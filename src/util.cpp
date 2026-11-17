#include "util.h"

#include <windows.h>

#include <cstdio>

std::wstring utf8_to_wide(const char* s, size_t len) {
  if (len == 0) return std::wstring();
  int wlen = MultiByteToWideChar(CP_UTF8, 0, s, static_cast<int>(len), nullptr, 0);
  std::wstring w(static_cast<size_t>(wlen), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s, static_cast<int>(len), &w[0], wlen);
  return w;
}

std::wstring utf8_to_wide(const std::string& s) {
  return utf8_to_wide(s.data(), s.size());
}

std::string wide_to_utf8(const std::wstring& w) {
  if (w.empty()) return std::string();
  int len = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()),
                                nullptr, 0, nullptr, nullptr);
  std::string s(static_cast<size_t>(len), '\0');
  WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()),
                      &s[0], len, nullptr, nullptr);
  return s;
}

std::string windows_error_message(unsigned long code) {
  wchar_t* buf = nullptr;
  DWORD len = FormatMessageW(
      FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
      nullptr, code, 0, reinterpret_cast<wchar_t*>(&buf), 0, nullptr);
  std::string text;
  if (len > 0 && buf) {
    std::wstring w(buf, len);
    while (!w.empty() && (w.back() == L'\r' || w.back() == L'\n' || w.back() == L' ')) w.pop_back();
    text = wide_to_utf8(w);
  }
  if (buf) LocalFree(buf);

  char num[32];
  snprintf(num, sizeof(num), "0x%08lX", code);
  if (text.empty()) return num;
  return text + " (" + num + ")";
}

void ensure_com() {
  static bool done = false;
  if (done) return;
  done = true;
  // 失敗しても、ここでは何もしない。COM を使う側の呼び出しが失敗して分かる。
  CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
}
