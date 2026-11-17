// マウスとキーボードの操作。SendInput（入力を送り込む Windows API）を使う。
//
// 送った「押した」は記録しておき、終了時や緊急停止のときに、押したままのものを離す。
// 緊急停止は別のスレッドから割り込むので、送信と記録はロックで守る。

#include "input.h"

#include <windows.h>

#include <cstdio>
#include <cstring>
#include <vector>

#include "app.h"
#include "util.h"

namespace input {

namespace {

SRWLOCK g_lock = SRWLOCK_INIT;
bool g_stopping = false;         // true になったら、もう何も送らない
unsigned char g_key_down[256];   // 0: 離している、1: 押している、2: 押している（拡張キー）
bool g_button_down[3];
POINT g_dry_position = {0, 0};   // テストモードでの、仮のカーソル位置

const DWORD kButtonDownFlag[3] = {MOUSEEVENTF_LEFTDOWN, MOUSEEVENTF_RIGHTDOWN,
                                  MOUSEEVENTF_MIDDLEDOWN};
const DWORD kButtonUpFlag[3] = {MOUSEEVENTF_LEFTUP, MOUSEEVENTF_RIGHTUP, MOUSEEVENTF_MIDDLEUP};

void track(const INPUT& in) {
  if (in.type == INPUT_KEYBOARD) {
    if (in.ki.dwFlags & KEYEVENTF_UNICODE) return;
    const WORD vk = in.ki.wVk;
    if (vk >= 256) return;
    if (in.ki.dwFlags & KEYEVENTF_KEYUP) {
      g_key_down[vk] = 0;
    } else {
      g_key_down[vk] = (in.ki.dwFlags & KEYEVENTF_EXTENDEDKEY) ? 2 : 1;
    }
  } else if (in.type == INPUT_MOUSE) {
    for (int b = 0; b < 3; b++) {
      if (in.mi.dwFlags & kButtonDownFlag[b]) g_button_down[b] = true;
      if (in.mi.dwFlags & kButtonUpFlag[b]) g_button_down[b] = false;
    }
  }
}

// --- 操作が届かないウィンドウの警告 ---
//
// Windows は、権限の低いプログラムから、管理者として動いているプログラムの
// ウィンドウへの入力を捨てる（UIPI という保護の仕組み）。このとき SendInput は
// 成功を返すので、送った側からは、届かなかったことが分からない。そこで、送る前に
// 相手のウィンドウを調べて、届かない見込みなら警告を出す。

const int kLevelUnknown = -1;
const int kLevelDenied = -2;

// プロセスの「整合性レベル」（権限の高さを表す値）を返す。ふつうのプログラムは
// 0x2000、管理者として動いているプログラムは 0x3000 である。
// 管理者として動いているプログラムは、権限の低い側からは調べることを拒まれる
// 場合が多い。そのときは kLevelDenied を返す。
int integrity_level(HANDLE process) {
  HANDLE token = nullptr;
  if (!OpenProcessToken(process, TOKEN_QUERY, &token)) {
    return GetLastError() == ERROR_ACCESS_DENIED ? kLevelDenied : kLevelUnknown;
  }
  alignas(void*) unsigned char buf[sizeof(TOKEN_MANDATORY_LABEL) + SECURITY_MAX_SID_SIZE];
  DWORD size = 0;
  int level = kLevelUnknown;
  if (GetTokenInformation(token, TokenIntegrityLevel, buf, sizeof(buf), &size)) {
    PSID sid = reinterpret_cast<TOKEN_MANDATORY_LABEL*>(buf)->Label.Sid;
    level = static_cast<int>(*GetSidSubAuthority(sid, *GetSidSubAuthorityCount(sid) - 1));
  }
  CloseHandle(token);
  return level;
}

// そのウィンドウが、自分より高い権限で動いているか。
bool is_higher_privileged(HWND window) {
  static const int own = integrity_level(GetCurrentProcess());
  if (own < 0 || own >= SECURITY_MANDATORY_HIGH_RID) return false;  // 自分が管理者なら届く

  DWORD pid = 0;
  GetWindowThreadProcessId(window, &pid);
  if (pid == 0 || pid == GetCurrentProcessId()) return false;
  HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
  if (!process) return false;
  const int level = integrity_level(process);
  CloseHandle(process);
  return level == kLevelDenied || level > own;
}

HWND g_checked_window = nullptr;  // 最後に調べたウィンドウ。同じ相手を何度も調べない

// これから送る入力の相手を調べ、届かない見込みなら警告を出す。
// キー操作の相手は前面のウィンドウ、マウスのボタンとホイールの相手は、カーソルの
// 下にあるウィンドウである。カーソルの移動だけなら届くので、調べない。
void warn_if_unreachable(const INPUT& in) {
  HWND window = nullptr;
  if (in.type == INPUT_KEYBOARD) {
    window = GetForegroundWindow();
  } else if (in.type == INPUT_MOUSE &&
             (in.mi.dwFlags &
              ~(MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK)) != 0) {
    POINT p;
    if (GetCursorPos(&p)) window = WindowFromPoint(p);
  }
  // ボタンなどの部品ではなく、それを含むウィンドウ全体で見る。
  if (window) window = GetAncestor(window, GA_ROOT);
  if (!window || window == g_checked_window) return;
  g_checked_window = window;
  if (!is_higher_privileged(window)) return;

  wchar_t title[128] = L"";
  GetWindowTextW(window, title, 128);
  fprintf(stderr,
          "[pikulix] 警告: 操作の相手のウィンドウ「%s」は、管理者として動いているようです。"
          "このままでは、クリックやキー入力が届きません。"
          "Pikulix を管理者として実行してください\n",
          wide_to_utf8(title).c_str());
}

bool send(INPUT* inputs, UINT count) {
  if (g_app.dry_run) return true;

  if (count > 0) warn_if_unreachable(inputs[0]);

  AcquireSRWLockExclusive(&g_lock);
  if (g_stopping) {
    // 緊急停止が始まっている。プロセスが終了させられるのを待つ。
    ReleaseSRWLockExclusive(&g_lock);
    Sleep(INFINITE);
  }
  const UINT sent = SendInput(count, inputs, sizeof(INPUT));
  for (UINT i = 0; i < sent; i++) track(inputs[i]);
  ReleaseSRWLockExclusive(&g_lock);
  return sent == count;
}

// extended は「拡張キー」かどうか。矢印キーや Delete などは、テンキーの同じキーと
// 区別するために、この印を付けて送る必要がある。
INPUT key_input(WORD vk, bool extended, bool up) {
  INPUT in = {};
  in.type = INPUT_KEYBOARD;
  in.ki.wVk = vk;
  in.ki.wScan = static_cast<WORD>(MapVirtualKeyW(vk, MAPVK_VK_TO_VSC));
  in.ki.dwFlags = (extended ? KEYEVENTF_EXTENDEDKEY : 0) | (up ? KEYEVENTF_KEYUP : 0);
  return in;
}

// ロックを取った状態で呼ぶ。
void release_locked() {
  for (int vk = 0; vk < 256; vk++) {
    if (!g_key_down[vk]) continue;
    INPUT in = key_input(static_cast<WORD>(vk), g_key_down[vk] == 2, true);
    SendInput(1, &in, sizeof(INPUT));
    g_key_down[vk] = 0;
  }
  for (int b = 0; b < 3; b++) {
    if (!g_button_down[b]) continue;
    INPUT in = {};
    in.type = INPUT_MOUSE;
    in.mi.dwFlags = kButtonUpFlag[b];
    SendInput(1, &in, sizeof(INPUT));
    g_button_down[b] = false;
  }
}

struct Key {
  WORD vk;
  bool extended;
};

struct KeyName {
  const char* name;
  WORD vk;
  bool extended;
};

const KeyName kKeyNames[] = {
    {"ctrl", VK_CONTROL, false},
    {"control", VK_CONTROL, false},
    {"shift", VK_SHIFT, false},
    {"alt", VK_MENU, false},
    {"win", VK_LWIN, true},
    {"enter", VK_RETURN, false},
    {"return", VK_RETURN, false},
    {"tab", VK_TAB, false},
    {"esc", VK_ESCAPE, false},
    {"escape", VK_ESCAPE, false},
    {"space", VK_SPACE, false},
    {"backspace", VK_BACK, false},
    {"bs", VK_BACK, false},
    {"delete", VK_DELETE, true},
    {"del", VK_DELETE, true},
    {"insert", VK_INSERT, true},
    {"ins", VK_INSERT, true},
    {"home", VK_HOME, true},
    {"end", VK_END, true},
    {"pageup", VK_PRIOR, true},
    {"pgup", VK_PRIOR, true},
    {"pagedown", VK_NEXT, true},
    {"pgdn", VK_NEXT, true},
    {"up", VK_UP, true},
    {"down", VK_DOWN, true},
    {"left", VK_LEFT, true},
    {"right", VK_RIGHT, true},
    {"printscreen", VK_SNAPSHOT, true},
    {"pause", VK_PAUSE, false},
    {"capslock", VK_CAPITAL, false},
    {"numlock", VK_NUMLOCK, true},
    {"scrolllock", VK_SCROLL, false},
    {"apps", VK_APPS, true},
    {"menu", VK_APPS, true},
    {"plus", VK_OEM_PLUS, false},
    {"minus", VK_OEM_MINUS, false},
    {"comma", VK_OEM_COMMA, false},
    {"period", VK_OEM_PERIOD, false},
    {"kanji", VK_KANJI, false},        // 半角/全角（IME のオン・オフ）
    {"henkan", VK_CONVERT, false},     // 変換
    {"muhenkan", VK_NONCONVERT, false} // 無変換
};

bool has_key(const std::vector<Key>& keys, WORD vk) {
  for (const Key& k : keys) {
    if (k.vk == vk) return true;
  }
  return false;
}

bool lookup_key(const std::string& token, std::vector<Key>& keys, std::string& error) {
  for (const KeyName& k : kKeyNames) {
    if (token == k.name) {
      keys.push_back(Key{k.vk, k.extended});
      return true;
    }
  }

  // f1 〜 f24
  if (token.size() >= 2 && token.size() <= 3 && token[0] == 'f') {
    int number = 0;
    bool digits = true;
    for (size_t i = 1; i < token.size(); i++) {
      if (token[i] < '0' || token[i] > '9') digits = false;
      number = number * 10 + (token[i] - '0');
    }
    if (digits && number >= 1 && number <= 24) {
      keys.push_back(Key{static_cast<WORD>(VK_F1 + number - 1), false});
      return true;
    }
  }

  // num0 〜 num9（テンキー）
  if (token.size() == 4 && token.compare(0, 3, "num") == 0 && token[3] >= '0' && token[3] <= '9') {
    keys.push_back(Key{static_cast<WORD>(VK_NUMPAD0 + (token[3] - '0')), false});
    return true;
  }

  if (token.size() == 1) {
    const unsigned char c = static_cast<unsigned char>(token[0]);
    if (c >= 'a' && c <= 'z') {
      keys.push_back(Key{static_cast<WORD>('A' + (c - 'a')), false});
      return true;
    }
    if (c >= '0' && c <= '9') {
      keys.push_back(Key{static_cast<WORD>(c), false});
      return true;
    }
    if (c > 0x20 && c < 0x7f) {
      // 記号は、いまのキーボード配列で、その文字を出すキーを調べる。
      // 配列によって場所が違うので、決め打ちにはしない。
      const SHORT r = VkKeyScanW(static_cast<wchar_t>(c));
      if (r != -1) {
        const BYTE state = HIBYTE(r);
        if ((state & 1) && !has_key(keys, VK_SHIFT)) keys.push_back(Key{VK_SHIFT, false});
        if ((state & 2) && !has_key(keys, VK_CONTROL)) keys.push_back(Key{VK_CONTROL, false});
        if ((state & 4) && !has_key(keys, VK_MENU)) keys.push_back(Key{VK_MENU, false});
        keys.push_back(Key{static_cast<WORD>(LOBYTE(r)), false});
        return true;
      }
    }
  }

  error = "キーの名前が分かりません: " + token;
  return false;
}

// "ctrl+shift+s" を、押す順のキーの並びにする。
bool parse_combo(const std::string& combo, std::vector<Key>& keys, std::string& error) {
  std::vector<std::string> tokens;
  std::string current;
  for (char ch : combo) {
    if (ch == '+' && !current.empty()) {
      tokens.push_back(current);
      current.clear();
    } else if (ch == '+') {
      current = "+";  // 区切りの直後の + は、+ のキーそのもの（"ctrl++"）
    } else if (ch != ' ') {
      current += (ch >= 'A' && ch <= 'Z') ? static_cast<char>(ch - 'A' + 'a') : ch;
    }
  }
  if (!current.empty()) tokens.push_back(current);

  if (tokens.empty()) {
    error = "キーが指定されていません";
    return false;
  }
  for (const std::string& token : tokens) {
    if (!lookup_key(token, keys, error)) return false;
  }
  return true;
}

bool send_keys(const std::vector<Key>& keys) {
  for (const Key& k : keys) {
    INPUT in = key_input(k.vk, k.extended, false);
    if (!send(&in, 1)) return false;
    Sleep(10);
  }
  Sleep(30);
  for (size_t i = keys.size(); i-- > 0;) {
    INPUT in = key_input(keys[i].vk, keys[i].extended, true);
    if (!send(&in, 1)) return false;
    Sleep(10);
  }
  return true;
}

// --- クリップボード ---

struct ClipboardItem {
  UINT format;
  std::vector<unsigned char> data;
};

bool open_clipboard() {
  // ほかのプログラムが開いている間は開けないので、少し待って繰り返す。
  for (int i = 0; i < 20; i++) {
    if (OpenClipboard(nullptr)) return true;
    Sleep(25);
  }
  return false;
}

// 中身がメモリのかたまりではない形式（ビットマップのハンドルなど）。
// これらは写しを取れないので、保存の対象から外す。画像は、メモリのかたまりである
// 別の形式（CF_DIB）でも入っているので、そちらで保存・復元される。
bool is_handle_format(UINT format) {
  switch (format) {
    case CF_BITMAP:
    case CF_METAFILEPICT:
    case CF_PALETTE:
    case CF_ENHMETAFILE:
    case CF_OWNERDISPLAY:
    case CF_DSPBITMAP:
    case CF_DSPMETAFILEPICT:
    case CF_DSPENHMETAFILE:
      return true;
  }
  return format >= CF_GDIOBJFIRST && format <= CF_GDIOBJLAST;
}

// クリップボードを開いた状態で呼ぶ。
void save_clipboard(std::vector<ClipboardItem>& items) {
  UINT format = 0;
  while ((format = EnumClipboardFormats(format)) != 0) {
    if (is_handle_format(format)) continue;
    HANDLE handle = GetClipboardData(format);
    if (!handle) continue;
    const SIZE_T size = GlobalSize(handle);
    const unsigned char* p = static_cast<const unsigned char*>(GlobalLock(handle));
    if (!p) continue;
    items.push_back(ClipboardItem{format, std::vector<unsigned char>(p, p + size)});
    GlobalUnlock(handle);
  }
}

// クリップボードを開いた状態で呼ぶ。
bool set_clipboard(UINT format, const void* data, size_t size) {
  HGLOBAL handle = GlobalAlloc(GMEM_MOVEABLE, size > 0 ? size : 1);
  if (!handle) return false;
  void* p = GlobalLock(handle);
  if (!p) {
    GlobalFree(handle);
    return false;
  }
  if (size > 0) memcpy(p, data, size);
  GlobalUnlock(handle);
  if (!SetClipboardData(format, handle)) {
    GlobalFree(handle);
    return false;
  }
  return true;  // 成功したら、メモリはクリップボードのものになる
}

}  // namespace

bool mouse_move(int x, int y) {
  if (g_app.dry_run) {
    g_dry_position.x = x;
    g_dry_position.y = y;
    return true;
  }

  // 絶対座標での移動は、仮想スクリーン全体を 0〜65535 に割り当てた値で指定する。
  // 目的の画素の中心を指すように換算する。
  const int vx = GetSystemMetrics(SM_XVIRTUALSCREEN);
  const int vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
  const int vw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
  const int vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
  if (vw <= 0 || vh <= 0) return false;

  INPUT in = {};
  in.type = INPUT_MOUSE;
  in.mi.dx = static_cast<LONG>((static_cast<long long>(x - vx) * 2 + 1) * 65536 / (2LL * vw));
  in.mi.dy = static_cast<LONG>((static_cast<long long>(y - vy) * 2 + 1) * 65536 / (2LL * vh));
  in.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;
  if (!send(&in, 1)) return false;

  // 換算の丸めで 1 画素ずれることがあるので、確かめて、ずれていたら直す。
  Sleep(10);
  POINT p;
  if (GetCursorPos(&p) && (p.x != x || p.y != y)) SetCursorPos(x, y);
  return true;
}

bool mouse_button(Button button, bool down) {
  int b = static_cast<int>(button);
  // 「主と副のボタンを切り替える」設定（左利き用）では、送るボタンを逆にする。
  if (b < 2 && GetSystemMetrics(SM_SWAPBUTTON)) b ^= 1;

  INPUT in = {};
  in.type = INPUT_MOUSE;
  in.mi.dwFlags = down ? kButtonDownFlag[b] : kButtonUpFlag[b];
  return send(&in, 1);
}

bool mouse_wheel(int notches) {
  INPUT in = {};
  in.type = INPUT_MOUSE;
  in.mi.dwFlags = MOUSEEVENTF_WHEEL;
  in.mi.mouseData = static_cast<DWORD>(notches * WHEEL_DELTA);
  return send(&in, 1);
}

void mouse_position(int& x, int& y) {
  POINT p = g_dry_position;
  if (!g_app.dry_run) GetCursorPos(&p);
  x = p.x;
  y = p.y;
}

bool type_text(const std::wstring& text) {
  for (size_t i = 0; i < text.size(); i++) {
    const wchar_t ch = text[i];
    if (ch == L'\r') continue;

    INPUT in[4] = {};
    UINT count = 2;
    if (ch == L'\n' || ch == L'\t') {
      // 改行とタブは、文字としてではなく、Enter キーと Tab キーとして送る。
      const WORD vk = (ch == L'\n') ? VK_RETURN : VK_TAB;
      in[0] = key_input(vk, false, false);
      in[1] = key_input(vk, false, true);
    } else {
      // KEYEVENTF_UNICODE は、キーではなく文字そのものを送る指定である。
      // キーボードの配列に関係なく、その文字が入力される。
      // UTF-16 で 2 つの値になる文字（絵文字など）は、2 つを続けて送る。
      const bool pair = ch >= 0xD800 && ch <= 0xDBFF && i + 1 < text.size();
      const int units = pair ? 2 : 1;
      for (int u = 0; u < units; u++) {
        for (int up = 0; up < 2; up++) {
          INPUT& k = in[u * 2 + up];
          k.type = INPUT_KEYBOARD;
          k.ki.wScan = text[i + u];
          k.ki.dwFlags = KEYEVENTF_UNICODE | (up ? KEYEVENTF_KEYUP : 0);
        }
      }
      count = static_cast<UINT>(units * 2);
      if (pair) i++;
    }
    if (!send(in, count)) return false;
    if (!g_app.dry_run) Sleep(5);
  }
  return true;
}

bool key_combo(const std::string& combo, std::string& error) {
  std::vector<Key> keys;
  if (!parse_combo(combo, keys, error)) return false;
  if (!send_keys(keys)) {
    error = "キー操作を送れませんでした";
    return false;
  }
  return true;
}

bool paste_text(const std::wstring& text, std::string& error) {
  if (g_app.dry_run) return true;

  std::vector<ClipboardItem> saved;
  if (!open_clipboard()) {
    error = "クリップボードを開けません";
    return false;
  }
  save_clipboard(saved);
  EmptyClipboard();
  bool ok = set_clipboard(CF_UNICODETEXT, text.c_str(), (text.size() + 1) * sizeof(wchar_t));
  // クリップボードの履歴（Win+V）に、貼り付ける文字列を残さないための印。
  const UINT exclude = RegisterClipboardFormatW(L"ExcludeClipboardContentFromMonitorProcessing");
  if (exclude) set_clipboard(exclude, "", 1);
  CloseClipboard();
  if (!ok) {
    error = "クリップボードに文字列を置けません";
    return false;
  }

  std::vector<Key> keys = {Key{VK_CONTROL, false}, Key{'V', false}};
  ok = send_keys(keys);
  if (!ok) error = "キー操作を送れませんでした";

  // 貼り付け先がクリップボードを読み終えるのを待ってから、元の内容に戻す。
  Sleep(300);
  if (open_clipboard()) {
    EmptyClipboard();
    for (const ClipboardItem& item : saved) {
      set_clipboard(item.format, item.data.data(), item.data.size());
    }
    CloseClipboard();
  }
  return ok;
}

void release_all() {
  if (g_app.dry_run) return;
  AcquireSRWLockExclusive(&g_lock);
  release_locked();
  ReleaseSRWLockExclusive(&g_lock);
}

void stop_and_release() {
  AcquireSRWLockExclusive(&g_lock);
  g_stopping = true;
  release_locked();
  ReleaseSRWLockExclusive(&g_lock);
}

}  // namespace input
