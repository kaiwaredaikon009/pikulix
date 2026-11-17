// コンソール（黒い画面）の扱い。

#pragma once

namespace console {

// 実行中だけ、コンソールの出力コードページ（文字コードの設定）を UTF-8 にする。
// mruby のエラーメッセージは UTF-8 のバイト列のまま標準エラーへ書かれるので、
// こうしないと日本語（ファイル名など）が文字化けする。
void use_utf8();

// コードページを元に戻す。どのスレッドから呼んでもよい。
void restore_code_page();

// コンソールのウィンドウを最小化する。操作対象を隠したり、キャプチャに写り込んだり
// しないようにするためである。最初に呼ばれたときだけ働く。
void minimize();

// minimize で最小化していたら、元に戻す。activate が true なら前面に出す。
void restore_window(bool activate);

// C ランタイムを通さずに、標準エラーへ UTF-8 の文字列を書く。
// 緊急停止のように、別のスレッドから割り込んで書くときに使う。
void write_error_raw(const char* utf8);

}  // namespace console
