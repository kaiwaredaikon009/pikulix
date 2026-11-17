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

void write_error_raw(const char* utf8) {
  HANDLE handle = GetStdHandle(STD_ERROR_HANDLE);
  if (!handle || handle == INVALID_HANDLE_VALUE) return;
  DWORD written = 0;
  WriteFile(handle, utf8, static_cast<DWORD>(strlen(utf8)), &written, nullptr);
}

}  // namespace console
