// 文字コードの変換など、どこからでも使う小さな関数。
//
// 文字列は、mruby 側では UTF-8、Windows API 側では UTF-16（ワイド文字）で扱う。

#pragma once

#include <string>

std::wstring utf8_to_wide(const char* s, size_t len);
std::wstring utf8_to_wide(const std::string& s);
std::string wide_to_utf8(const std::wstring& w);

// Windows のエラーコード（GetLastError の値）を、説明の文字列（UTF-8）にする。
std::string windows_error_message(unsigned long code);

// COM（Windows の部品を呼び出す仕組み）を、このスレッドで使えるようにする。
// 画像の読み書き（WIC）とアプリの起動（ShellExecute）で要る。何度呼んでもよい。
void ensure_com();
