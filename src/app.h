// 実行全体で共有する状態。

#pragma once

#include <string>

#include "image.h"

struct App {
  std::wstring script_dir;  // スクリプトのあるディレクトリ。画像のパスの基準
  std::wstring exe_dir;     // pikulix.exe のあるディレクトリ。ログなどの置き場所

  // テストモード（--screen-image）。画面の代わりに fake_screen を探し、入力は送らない。
  bool dry_run = false;
  Image fake_screen;

  // 緊急停止のホットキー（Alt+Shift+C）を登録できたか。
  // できていないときは、入力を送らない（止める手段が無いと危険なため）。
  bool hotkey_ok = false;
};

extern App g_app;
