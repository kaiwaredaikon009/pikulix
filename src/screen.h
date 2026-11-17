// 画面キャプチャ。

#pragma once

#include <string>

#include "image.h"

// プライマリモニタ（主となる画面）を写し取る。
//
// origin_x、origin_y には、写し取った範囲の左上の、仮想スクリーン座標（複数の
// 画面をまとめた座標系）での位置が入る。プライマリモニタでは (0, 0) である。
//
// マウスカーソルは写らない。
bool capture_screen(Image& out, int& origin_x, int& origin_y, std::string& error);
