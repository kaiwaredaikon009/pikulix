// スクリプトを選ぶダイアログ。exe をダブルクリックして起動したときに出す。

#pragma once

#include <string>

// Windows 標準のファイル選択ダイアログを出し、選ばれたファイルのフルパスを path に入れる。
// 選ばれたら true を返す。キャンセルされたら false を返し、error は空のままにする。
// ダイアログを出せなかったら false を返し、error に理由（UTF-8）を入れる。
// default_dir は、Windows が前に開いたフォルダを覚えていないときに開くフォルダ。
bool choose_script(const std::wstring& default_dir, std::wstring& path, std::string& error);
