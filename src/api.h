// スクリプトから呼ぶ関数のうち、C++ で実装するもの。

#pragma once

struct mrb_state;

// mruby に関数を登録する。src/prelude.rb を読み込む前に呼ぶ。
void define_api(mrb_state* mrb);
