#include "console.h"

#include <windows.h>

#include <atomic>
#include <cstring>

namespace console {

namespace {

std::atomic<UINT> g_original_code_page{0};
std::atomic<HWND> g_minimized_window{nullptr};
std::atomic<bool> g_minimize_done{false};

}  // namespace

void use_utf8() {
  UINT original = GetConsoleOutputCP();  // コンソールが無ければ 0
  if (original == 0 || original == CP_UTF8) return;
  g_original_code_page = original;
  SetConsoleOutputCP(CP_UTF8);
}

void restore_code_page() {
  UINT original = g_original_code_page.exchange(0);
  if (original != 0) SetConsoleOutputCP(original);
}

void minimize() {
  if (g_minimize_done.exchange(true)) return;

  // Windows Terminal などの中で動いているときは、ここで得られるのは目に見えない
  // 代理のウィンドウである。それを最小化すると、Windows Terminal に要求が伝わる。
  HWND window = GetConsoleWindow();
  if (!window || IsIconic(window)) return;

  // SW_MINIMIZE は、最小化したうえで、次のウィンドウを前面にする。
  ShowWindow(window, SW_MINIMIZE);
  g_minimized_window = window;
  Sleep(300);  // 最小化のアニメーションが終わるのを待つ
}

void restore_window(bool activate) {
  HWND window = g_minimized_window.exchange(nullptr);
  if (window) ShowWindow(window, activate ? SW_RESTORE : SW_SHOWNOACTIVATE);
}

bool owns_console_alone() {
  // GetConsoleProcessList は、このコンソールを使っているプロセスの数を返す。
  // 配列が足りないときも、必要な数（つまりプロセスの数）を返す。
  DWORD processes[2];
  if (GetConsoleProcessList(processes, 2) != 1) return false;

  // GetConsoleMode は、ハンドルが本物のコンソールのときだけ成功する。
  DWORD mode = 0;
  return GetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), &mode) &&
         GetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE), &mode) &&
         GetConsoleMode(GetStdHandle(STD_ERROR_HANDLE), &mode);
}

void wait_for_enter() {
  HANDLE in = GetStdHandle(STD_INPUT_HANDLE);
  DWORD mode = 0;
  if (!GetConsoleMode(in, &mode)) return;

  write_error_raw("\nEnter キーを押すと閉じます。");

  // 実行中に押されていたキーを捨ててから、1 行ぶんの入力（Enter まで）を待つ。
  FlushConsoleInputBuffer(in);
  SetConsoleMode(in, mode | ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT | ENABLE_PROCESSED_INPUT);
  wchar_t buf[256];
  DWORD n = 0;
  while (ReadConsoleW(in, buf, 256, &n, nullptr) && n > 0) {
    if (buf[n - 1] == L'\n') break;  // 長い行は、何回かに分けて届く
  }
  SetConsoleMode(in, mode);
}

void write_error_raw(const char* utf8) {
  HANDLE handle = GetStdHandle(STD_ERROR_HANDLE);
  if (!handle || handle == INVALID_HANDLE_VALUE) return;
  DWORD written = 0;
  WriteFile(handle, utf8, static_cast<DWORD>(strlen(utf8)), &written, nullptr);
}

}  // namespace console
