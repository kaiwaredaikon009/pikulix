// マウスとキーボードの操作。
//
// テストモード（g_app.dry_run）では、実際には何も送らない。

#pragma once

#include <string>

namespace input {

enum Button { kLeft = 0, kRight = 1, kMiddle = 2 };

// マウスカーソルを (x, y) へ動かす。座標は仮想スクリーン座標（物理ピクセル）。
bool mouse_move(int x, int y);
bool mouse_button(Button button, bool down);
// ホイールを回す。notches が正なら奥（上）へ、負なら手前（下）へ。
bool mouse_wheel(int notches);
void mouse_position(int& x, int& y);

// 文字列を入力する。キーボードの配列には依存しない。
bool type_text(const std::wstring& text);

// "ctrl+s" のようなキー操作を送る。書き方が正しくなければ false を返し、
// error に理由を入れる。テストモードでも、書き方の確認は行う。
bool key_combo(const std::string& combo, std::string& error);

// クリップボード経由で文字列を貼り付ける（Ctrl+V を送る）。
// クリップボードの元の内容は、貼り付けのあとで戻す。
bool paste_text(const std::wstring& text, std::string& error);

// 押したままになっているキーとマウスボタンを、すべて離す。
void release_all();

// これ以降の入力を止めて、押したままのキーとマウスボタンを離す。
// 緊急停止のとき、別のスレッドから呼ぶ。
void stop_and_release();

}  // namespace input
