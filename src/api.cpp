// スクリプトから呼ぶ関数のうち、C++ で実装するもの。
//
// ここで定義するのは、Pikulix._find のような単純な関数だけである。find や click
// など、スクリプトから使うメソッドは、これらを組み合わせて src/prelude.rb で定義する。
//
// 注意: mruby の例外は longjmp で実装されている。mruby の API が例外を投げると、
// C++ のデストラクタが呼ばれないまま関数を抜ける。そのため、mruby から呼ばれる関数
// （pk_ で始まるもの）は、次の形にそろえる。
//
//   1. 最初に mrb_get_args で引数を受け取る（ここで例外になりうる）。
//   2. 実際の処理は do_ で始まる関数で行う。C++ のオブジェクトはその中だけで使う。
//      失敗したら、g_error に理由を入れて false を返す。
//   3. false が返ったら raise_error で例外を投げる。
//
// pk_ の関数の中では、std::string などのデストラクタを持つ変数を作らないこと。

#include "api.h"

#include <windows.h>
#include <shellapi.h>

#include <cstdio>
#include <cstring>
#include <cwchar>
#include <map>
#include <string>

#include <mruby.h>
#include <mruby/array.h>
#include <mruby/string.h>

#include "app.h"
#include "console.h"
#include "image.h"
#include "input.h"
#include "match.h"
#include "screen.h"
#include "util.h"

namespace {

std::string g_error;   // 直前の失敗の理由
std::string g_result;  // 文字列を返す関数の結果

std::map<std::wstring, Image> g_templates;  // 読み込んだ画像。パスが鍵
Image g_screen;                             // 最後に写し取った画面
bool g_has_screen = false;

mrb_noreturn void raise_error(mrb_state* mrb) {
  struct RClass* pikulix = mrb_module_get(mrb, "Pikulix");
  mrb_raise(mrb, mrb_class_get_under(mrb, pikulix, "Error"), g_error.c_str());
}

bool is_absolute(const std::wstring& path) {
  if (path.size() >= 2 && path[1] == L':') return true;
  return !path.empty() && (path[0] == L'\\' || path[0] == L'/');
}

// 画像のパスは、スクリプトのあるディレクトリからの相対で解決する。
std::wstring resolve_path(const std::wstring& path) {
  if (is_absolute(path) || g_app.script_dir.empty()) return path;
  return g_app.script_dir + L"\\" + path;
}

// 入力を送る前の準備。
bool prepare_input() {
  if (g_app.dry_run) return true;
  if (!g_app.hotkey_ok) {
    g_error =
        "緊急停止のホットキー（Alt+Shift+C）を登録できなかったので、操作を中止しました。"
        "ほかのプログラム（別の Pikulix など）が同じキーを使っていないか確認してください";
    return false;
  }
  console::minimize();
  return true;
}

double clock_seconds() {
  LARGE_INTEGER freq, now;
  QueryPerformanceFrequency(&freq);
  QueryPerformanceCounter(&now);
  return static_cast<double>(now.QuadPart) / static_cast<double>(freq.QuadPart);
}

// --- 探す ---

struct FindOutcome {
  bool found;
  int x, y, w, h;
  bool has_score;
  double score;
  double ms;
};

bool do_find(const char* path, size_t len, double similarity, FindOutcome& out) {
  const std::wstring full = resolve_path(utf8_to_wide(path, len));
  auto it = g_templates.find(full);
  if (it == g_templates.end()) {
    Image image;
    std::string error;
    if (!load_image(full, image, error)) {
      g_error = "画像を読めません: " + wide_to_utf8(full) + "（" + error + "）";
      return false;
    }
    it = g_templates.emplace(full, std::move(image)).first;
  }
  const Image& needle = it->second;

  const Image* screen = &g_app.fake_screen;
  int origin_x = 0, origin_y = 0;
  if (!g_app.dry_run) {
    console::minimize();
    std::string error;
    if (!capture_screen(g_screen, origin_x, origin_y, error)) {
      g_error = error;
      return false;
    }
    g_has_screen = true;
    screen = &g_screen;
  }

  const double start = clock_seconds();
  const MatchResult r = find_template(*screen, needle, similarity);
  out.ms = (clock_seconds() - start) * 1000.0;
  out.found = r.found;
  out.x = origin_x + r.x;
  out.y = origin_y + r.y;
  out.w = needle.width;
  out.h = needle.height;
  out.has_score = r.has_score;
  out.score = r.score;
  return true;
}

// Pikulix._find(path, similarity)
//   => [found, x, y, w, h, score, ms]
// score は最も近い候補の一致度（候補が無ければ nil）。ms は探索にかかった時間。
mrb_value pk_find(mrb_state* mrb, mrb_value /*self*/) {
  const char* path = nullptr;
  mrb_int len = 0;
  mrb_float similarity = 0;
  mrb_get_args(mrb, "sf", &path, &len, &similarity);

  FindOutcome out = {};
  if (!do_find(path, static_cast<size_t>(len), similarity, out)) raise_error(mrb);

  mrb_value ary = mrb_ary_new_capa(mrb, 7);
  mrb_ary_push(mrb, ary, mrb_bool_value(out.found));
  mrb_ary_push(mrb, ary, mrb_int_value(mrb, out.x));
  mrb_ary_push(mrb, ary, mrb_int_value(mrb, out.y));
  mrb_ary_push(mrb, ary, mrb_int_value(mrb, out.w));
  mrb_ary_push(mrb, ary, mrb_int_value(mrb, out.h));
  mrb_ary_push(mrb, ary, out.has_score ? mrb_float_value(mrb, out.score) : mrb_nil_value());
  mrb_ary_push(mrb, ary, mrb_float_value(mrb, out.ms));
  return ary;
}

// ファイル名に使えない文字を除き、ディレクトリと拡張子を落とす。
std::wstring safe_label(std::wstring name) {
  const size_t slash = name.find_last_of(L"\\/");
  if (slash != std::wstring::npos) name = name.substr(slash + 1);
  const size_t dot = name.find_last_of(L'.');
  if (dot != std::wstring::npos && dot > 0) name = name.substr(0, dot);
  for (wchar_t& c : name) {
    if (c < 0x20 || wcschr(L"\\/:*?\"<>|", c)) c = L'_';
  }
  if (name.size() > 60) name.resize(60);
  return name;
}

// 最後に探した画面を、exe のあるディレクトリの logs に保存する。
// 保存したら g_result にパスを入れる。保存するものが無ければ、g_result は空になる。
bool do_save_screen(const char* label, size_t len) {
  g_result.clear();
  const Image* screen = g_app.dry_run ? &g_app.fake_screen : (g_has_screen ? &g_screen : nullptr);
  if (!screen || screen->width <= 0) return true;

  const std::wstring dir = g_app.exe_dir + L"\\logs";
  CreateDirectoryW(dir.c_str(), nullptr);  // すでにあれば失敗するが、かまわない

  SYSTEMTIME t;
  GetLocalTime(&t);
  wchar_t stamp[32];
  swprintf(stamp, 32, L"%04d%02d%02d-%02d%02d%02d", t.wYear, t.wMonth, t.wDay, t.wHour,
           t.wMinute, t.wSecond);
  const std::wstring base =
      dir + L"\\failed-" + stamp + L"-" + safe_label(utf8_to_wide(label, len));

  // 同じ秒に同じ名前で保存するときは、番号を付ける。
  std::wstring path = base + L".png";
  for (int i = 2; i < 100 && GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES; i++) {
    path = base + L"-" + std::to_wstring(i) + L".png";
  }

  std::string error;
  if (!save_png(path, *screen, error)) {
    g_error = "画面を保存できません: " + wide_to_utf8(path) + "（" + error + "）";
    return false;
  }
  g_result = wide_to_utf8(path);
  return true;
}

// Pikulix._save_screen(label) => 保存したパス、または nil
mrb_value pk_save_screen(mrb_state* mrb, mrb_value /*self*/) {
  const char* label = nullptr;
  mrb_int len = 0;
  mrb_get_args(mrb, "s", &label, &len);

  if (!do_save_screen(label, static_cast<size_t>(len))) raise_error(mrb);
  if (g_result.empty()) return mrb_nil_value();
  return mrb_str_new(mrb, g_result.data(), static_cast<mrb_int>(g_result.size()));
}

// --- マウス ---

bool do_mouse_move(int x, int y) {
  if (!prepare_input()) return false;
  if (!input::mouse_move(x, y)) {
    g_error = "マウスを動かせませんでした";
    return false;
  }
  return true;
}

// Pikulix._mouse_move(x, y)
mrb_value pk_mouse_move(mrb_state* mrb, mrb_value /*self*/) {
  mrb_int x = 0, y = 0;
  mrb_get_args(mrb, "ii", &x, &y);
  if (!do_mouse_move(static_cast<int>(x), static_cast<int>(y))) raise_error(mrb);
  return mrb_nil_value();
}

bool do_mouse_button(int button, bool down) {
  if (button < 0 || button > 2) {
    g_error = "マウスのボタンの指定が正しくありません";
    return false;
  }
  if (!prepare_input()) return false;
  if (!input::mouse_button(static_cast<input::Button>(button), down)) {
    g_error = "マウスのボタンを操作できませんでした";
    return false;
  }
  return true;
}

// Pikulix._mouse_button(button, down)  button は 0: 左、1: 右、2: 中
mrb_value pk_mouse_button(mrb_state* mrb, mrb_value /*self*/) {
  mrb_int button = 0;
  mrb_bool down = FALSE;
  mrb_get_args(mrb, "ib", &button, &down);
  if (!do_mouse_button(static_cast<int>(button), down)) raise_error(mrb);
  return mrb_nil_value();
}

bool do_mouse_wheel(int notches) {
  if (!prepare_input()) return false;
  if (!input::mouse_wheel(notches)) {
    g_error = "ホイールを回せませんでした";
    return false;
  }
  return true;
}

// Pikulix._mouse_wheel(notches)  正なら上、負なら下
mrb_value pk_mouse_wheel(mrb_state* mrb, mrb_value /*self*/) {
  mrb_int notches = 0;
  mrb_get_args(mrb, "i", &notches);
  if (!do_mouse_wheel(static_cast<int>(notches))) raise_error(mrb);
  return mrb_nil_value();
}

// Pikulix._mouse_pos => [x, y]
mrb_value pk_mouse_pos(mrb_state* mrb, mrb_value /*self*/) {
  int x = 0, y = 0;
  input::mouse_position(x, y);
  mrb_value ary = mrb_ary_new_capa(mrb, 2);
  mrb_ary_push(mrb, ary, mrb_int_value(mrb, x));
  mrb_ary_push(mrb, ary, mrb_int_value(mrb, y));
  return ary;
}

// --- キーボード ---

bool do_type(const char* text, size_t len) {
  if (!prepare_input()) return false;
  if (!input::type_text(utf8_to_wide(text, len))) {
    g_error = "文字列を入力できませんでした";
    return false;
  }
  return true;
}

// Pikulix._type(text)
mrb_value pk_type(mrb_state* mrb, mrb_value /*self*/) {
  const char* text = nullptr;
  mrb_int len = 0;
  mrb_get_args(mrb, "s", &text, &len);
  if (!do_type(text, static_cast<size_t>(len))) raise_error(mrb);
  return mrb_nil_value();
}

bool do_key(const char* combo, size_t len) {
  if (!prepare_input()) return false;
  std::string error;
  if (!input::key_combo(std::string(combo, len), error)) {
    g_error = error;
    return false;
  }
  return true;
}

// Pikulix._key(combo)  combo は "ctrl+s" のような文字列
mrb_value pk_key(mrb_state* mrb, mrb_value /*self*/) {
  const char* combo = nullptr;
  mrb_int len = 0;
  mrb_get_args(mrb, "s", &combo, &len);
  if (!do_key(combo, static_cast<size_t>(len))) raise_error(mrb);
  return mrb_nil_value();
}

bool do_paste(const char* text, size_t len) {
  if (!prepare_input()) return false;
  std::string error;
  if (!input::paste_text(utf8_to_wide(text, len), error)) {
    g_error = error;
    return false;
  }
  return true;
}

// Pikulix._paste(text)
mrb_value pk_paste(mrb_state* mrb, mrb_value /*self*/) {
  const char* text = nullptr;
  mrb_int len = 0;
  mrb_get_args(mrb, "s", &text, &len);
  if (!do_paste(text, static_cast<size_t>(len))) raise_error(mrb);
  return mrb_nil_value();
}

// --- ファイル ---
//
// スクリプトから読み書きするのはテキストだけで、文字コードは UTF-8 とする。
// mruby-io（File）は、Windows でネットワーク用の DLL を要するので使っていない
// （mruby_build_config.rb 参照）。パスはワイド文字 API で扱い、日本語も通る。

// ファイルのパスは、画像と同じく、スクリプトのあるディレクトリからの相対で解決する。
std::wstring resolve_file(const char* path, size_t len) {
  return resolve_path(utf8_to_wide(path, len));
}

bool read_whole_file(const std::wstring& path, std::string& out) {
  HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                         OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) return false;
  char buf[65536];
  DWORD n = 0;
  bool ok = true;
  while ((ok = ReadFile(h, buf, sizeof(buf), &n, nullptr)) && n > 0) out.append(buf, n);
  const DWORD error = GetLastError();
  CloseHandle(h);
  if (!ok) SetLastError(error);
  return ok;
}

bool valid_utf8(const std::string& s) {
  if (s.empty()) return true;
  return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()),
                             nullptr, 0) > 0;
}

// テキストを読み、UTF-8 にして g_result に入れる。
//   - 先頭の UTF-8 の BOM は取り除く（メモ帳や Excel の「CSV UTF-8」が付ける）。
//   - UTF-16 LE（BOM 付き）は UTF-8 に変換する（メモ帳の「UTF-16 LE」、Excel の
//     「Unicode テキスト」が、この形式で保存する）。
//   - それ以外で UTF-8 として正しくないもの（Shift_JIS など）は、エラーにする。
//     化けた文字列を黙って返すより、理由が分かるほうがよい。
bool do_read_text(const char* path, size_t len) {
  g_result.clear();
  const std::wstring full = resolve_file(path, len);
  std::string data;
  if (!read_whole_file(full, data)) {
    g_error = "ファイルを読めません: " + wide_to_utf8(full) + "（" +
              windows_error_message(GetLastError()) + "）";
    return false;
  }
  if (data.compare(0, 2, "\xFF\xFE") == 0) {
    const size_t units = (data.size() - 2) / 2;
    std::wstring w(units, L'\0');
    if (units > 0) memcpy(&w[0], data.data() + 2, units * 2);
    g_result = wide_to_utf8(w);
    return true;
  }
  if (data.compare(0, 3, "\xEF\xBB\xBF") == 0) data.erase(0, 3);
  if (!valid_utf8(data)) {
    g_error = "ファイルの文字コードが UTF-8 ではありません（Shift_JIS で保存されたものなど）: " +
              wide_to_utf8(full);
    return false;
  }
  g_result = std::move(data);
  return true;
}

// Pikulix._read_text(path) => ファイルの中身（UTF-8 の文字列）
mrb_value pk_read_text(mrb_state* mrb, mrb_value /*self*/) {
  const char* path = nullptr;
  mrb_int len = 0;
  mrb_get_args(mrb, "s", &path, &len);
  if (!do_read_text(path, static_cast<size_t>(len))) raise_error(mrb);
  return mrb_str_new(mrb, g_result.data(), static_cast<mrb_int>(g_result.size()));
}

// テキストを UTF-8 のまま書く（BOM は付けず、改行も変換しない）。
// ファイルが無ければ作る。append が偽なら上書きし、真なら末尾に書き足す。
bool do_write_text(const char* path, size_t len, const char* text, size_t text_len, bool append) {
  const std::wstring full = resolve_file(path, len);
  HANDLE h = CreateFileW(full.c_str(), append ? FILE_APPEND_DATA : GENERIC_WRITE, FILE_SHARE_READ,
                         nullptr, append ? OPEN_ALWAYS : CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL,
                         nullptr);
  if (h == INVALID_HANDLE_VALUE) {
    g_error = "ファイルに書けません: " + wide_to_utf8(full) + "（" +
              windows_error_message(GetLastError()) + "）";
    return false;
  }
  bool ok = true;
  size_t done = 0;
  while (ok && done < text_len) {
    DWORD n = 0;
    ok = WriteFile(h, text + done, static_cast<DWORD>(text_len - done), &n, nullptr) && n > 0;
    done += n;
  }
  const DWORD error = GetLastError();
  CloseHandle(h);
  if (!ok) {
    g_error = "ファイルに書けません: " + wide_to_utf8(full) + "（" +
              windows_error_message(error) + "）";
    return false;
  }
  return true;
}

// Pikulix._write_text(path, text, append)
mrb_value pk_write_text(mrb_state* mrb, mrb_value /*self*/) {
  const char* path = nullptr;
  mrb_int len = 0;
  const char* text = nullptr;
  mrb_int text_len = 0;
  mrb_bool append = FALSE;
  mrb_get_args(mrb, "ssb", &path, &len, &text, &text_len, &append);
  if (!do_write_text(path, static_cast<size_t>(len), text, static_cast<size_t>(text_len),
                     append)) {
    raise_error(mrb);
  }
  return mrb_nil_value();
}

bool do_file_exist(const char* path, size_t len) {
  const DWORD attr = GetFileAttributesW(resolve_file(path, len).c_str());
  return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
}

// Pikulix._file_exist(path) => ファイルがあれば true（ディレクトリは false）
mrb_value pk_file_exist(mrb_state* mrb, mrb_value /*self*/) {
  const char* path = nullptr;
  mrb_int len = 0;
  mrb_get_args(mrb, "s", &path, &len);
  return mrb_bool_value(do_file_exist(path, static_cast<size_t>(len)));
}

// --- その他 ---

// Pikulix._sleep(seconds)
mrb_value pk_sleep(mrb_state* mrb, mrb_value /*self*/) {
  mrb_float seconds = 0;
  mrb_get_args(mrb, "f", &seconds);
  if (seconds > 0) {
    const double ms = seconds * 1000.0;
    Sleep(ms >= 4000000000.0 ? INFINITE : static_cast<DWORD>(ms + 0.5));
  }
  return mrb_nil_value();
}

// Pikulix._clock => 経過時間をはかるための秒数（起点に意味はない）
mrb_value pk_clock(mrb_state* mrb, mrb_value /*self*/) {
  return mrb_float_value(mrb, clock_seconds());
}

bool do_run(const char* file, size_t file_len, const char* params, size_t params_len) {
  if (g_app.dry_run) return true;

  const std::wstring wfile = utf8_to_wide(file, file_len);
  const std::wstring wparams = utf8_to_wide(params, params_len);

  // ShellExecuteEx は、エクスプローラでファイルを開くのと同じ仕組みで起動する。
  // プログラムのほか、文書や URL も、関連付けられたアプリで開ける。
  ensure_com();
  SHELLEXECUTEINFOW info = {};
  info.cbSize = sizeof(info);
  info.fMask = SEE_MASK_FLAG_NO_UI | SEE_MASK_NOASYNC;  // 失敗してもダイアログを出さない
  info.lpFile = wfile.c_str();
  info.lpParameters = wparams.empty() ? nullptr : wparams.c_str();
  info.lpDirectory = g_app.script_dir.empty() ? nullptr : g_app.script_dir.c_str();
  info.nShow = SW_SHOWNORMAL;
  if (!ShellExecuteExW(&info)) {
    g_error = "起動できません: " + std::string(file, file_len) + "（" +
              windows_error_message(GetLastError()) + "）";
    return false;
  }
  return true;
}

// Pikulix._run(file, params)
mrb_value pk_run(mrb_state* mrb, mrb_value /*self*/) {
  const char* file = nullptr;
  mrb_int file_len = 0;
  const char* params = nullptr;
  mrb_int params_len = 0;
  mrb_get_args(mrb, "ss", &file, &file_len, &params, &params_len);
  if (!do_run(file, static_cast<size_t>(file_len), params, static_cast<size_t>(params_len))) {
    raise_error(mrb);
  }
  return mrb_nil_value();
}

void do_message_box(const char* text, size_t text_len, const char* title, size_t title_len) {
  const std::wstring wtext = utf8_to_wide(text, text_len);
  const std::wstring wtitle = utf8_to_wide(title, title_len);
  MessageBoxW(nullptr, wtext.c_str(), wtitle.c_str(),
              MB_OK | MB_ICONINFORMATION | MB_SETFOREGROUND);
}

// message_box(text, title = "Pikulix")
// メッセージボックスを表示し、OK が押されるまで待つ。
mrb_value pk_message_box(mrb_state* mrb, mrb_value /*self*/) {
  const char* text = nullptr;
  mrb_int text_len = 0;
  const char* title = "Pikulix";
  mrb_int title_len = 7;
  mrb_get_args(mrb, "s|s", &text, &text_len, &title, &title_len);
  do_message_box(text, static_cast<size_t>(text_len), title, static_cast<size_t>(title_len));
  return mrb_nil_value();
}

}  // namespace

void define_api(mrb_state* mrb) {
  struct RClass* pikulix = mrb_define_module(mrb, "Pikulix");
  mrb_define_module_function(mrb, pikulix, "_find", pk_find, MRB_ARGS_REQ(2));
  mrb_define_module_function(mrb, pikulix, "_save_screen", pk_save_screen, MRB_ARGS_REQ(1));
  mrb_define_module_function(mrb, pikulix, "_mouse_move", pk_mouse_move, MRB_ARGS_REQ(2));
  mrb_define_module_function(mrb, pikulix, "_mouse_button", pk_mouse_button, MRB_ARGS_REQ(2));
  mrb_define_module_function(mrb, pikulix, "_mouse_wheel", pk_mouse_wheel, MRB_ARGS_REQ(1));
  mrb_define_module_function(mrb, pikulix, "_mouse_pos", pk_mouse_pos, MRB_ARGS_NONE());
  mrb_define_module_function(mrb, pikulix, "_type", pk_type, MRB_ARGS_REQ(1));
  mrb_define_module_function(mrb, pikulix, "_key", pk_key, MRB_ARGS_REQ(1));
  mrb_define_module_function(mrb, pikulix, "_paste", pk_paste, MRB_ARGS_REQ(1));
  mrb_define_module_function(mrb, pikulix, "_sleep", pk_sleep, MRB_ARGS_REQ(1));
  mrb_define_module_function(mrb, pikulix, "_clock", pk_clock, MRB_ARGS_NONE());
  mrb_define_module_function(mrb, pikulix, "_run", pk_run, MRB_ARGS_REQ(2));
  mrb_define_module_function(mrb, pikulix, "_read_text", pk_read_text, MRB_ARGS_REQ(1));
  mrb_define_module_function(mrb, pikulix, "_write_text", pk_write_text, MRB_ARGS_REQ(3));
  mrb_define_module_function(mrb, pikulix, "_file_exist", pk_file_exist, MRB_ARGS_REQ(1));

  mrb_define_private_method(mrb, mrb->kernel_module, "message_box", pk_message_box,
                            MRB_ARGS_ARG(1, 1));
}
