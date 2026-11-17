// テンプレートマッチング。画面や入力には依存しない純粋な関数である。

#pragma once

#include "image.h"

struct MatchResult {
  bool found = false;      // しきい値以上の場所があったか
  int x = 0;               // 見つけた場所の左上（探される側の画像の中での座標）
  int y = 0;
  bool has_score = false;  // score が有効か（候補がまったく無ければ false）
  double score = 0.0;      // 最も近かった場所の一致度。found が false でも入る
};

// haystack（探される側。画面）の中から needle（探す画像）に最も近い場所を探す。
//
// 一致度は 1.0 が完全一致で、おおよそ次の積である（詳しくは match.cpp）。
//   - 形の一致度: 正規化相互相関に、コントラストの違いを加味したもの
//   - 色の一致度: 平均の色の近さ
// 完全に一致する場所が複数あるときは、いちばん上、その中でいちばん左を返す。
MatchResult find_template(const Image& haystack, const Image& needle, double threshold);

// 指定した場所での一致度を返す。find_template が最後に使う計算と同じものである。
double match_score_at(const Image& haystack, const Image& needle, int x, int y);
