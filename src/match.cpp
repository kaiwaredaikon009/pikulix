// テンプレートマッチング。
//
// ■ 一致度
//
// 探す画像 t と、画面から切り出した同じ大きさの領域 c について、
//
//   形の一致度 = (2 * cov(t, c) + F) / (var(t) + var(c) + F)
//   色の一致度 = max(0, 1 - (平均の色の差 / 64)^2)
//   一致度     = 形の一致度 * 色の一致度
//
// とする。var は分散、cov は共分散で、B・G・R の 3 色それぞれで求めて平均する。
//
// - 形の一致度は、t と c のコントラストが同じなら、正規化相互相関（OpenCV の
//   TM_CCOEFF_NORMED）と同じ値になる。コントラストが違うと、それより小さくなる。
//   正規化相互相関だけでは、黒い文字と灰色の文字（有効なボタンと無効なボタン）が
//   区別できないためである。
// - F は小さな定数である。t も c も単色のとき、正規化相互相関は 0 / 0 になって
//   求められないが、この式なら 1（一致）になる。単色に近い画像を扱うための項である。
// - 色の一致度は、形が同じで色だけが違うもの（青いボタンと灰色のボタン）を区別する。
//   平均の色が 64 階調（256 階調のうち）離れると 0 になる。
//
// ■ 速くする工夫
//
// すべての場所で上の計算をすると遅すぎるので、3 段階で絞り込む。
//
//   1. 粗い探索: 画像を明るさだけにし、小さな四角ごとの平均（ぼかしたもの）を、
//      間引いた点（100 点前後）だけで比べる。すべての場所について行う。
//   2. 絞り込み: 残った候補を、もっと細かい点で比べ直す（大きな画像のときだけ）。
//   3. 判定: 残った候補について、上の式をすべての画素と色で計算する。
//
// 縮小した画像どうしを比べる方法と違い、場所は 1 画素ずつずらして調べる。縮小の
// 格子と位置がずれて見落とす、ということが起きない。完全に一致する場所は、粗い
// 探索でも一致度が 1 になるので、必ず最後まで残る。

#include "match.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace {

const double kVarFloor = 4.0;     // 上の式の F（階調の 2 乗が単位）
const double kColorRange = 64.0;  // 色の一致度が 0 になる、平均の色の差

const int kMinTaps = 32;               // 粗い探索で比べる点の数の下限
const int kMaxTaps = 128;              // 同、上限。探索の時間はこれに比例する
const int kMaxFineTaps = 1024;         // 絞り込みで比べる点の数の上限
const int kFineStageMinPixels = 8192;  // これより大きな画像だけ、絞り込みを行う
const size_t kMaxCoarseCandidates = 2000;
const size_t kMaxFineCandidates = 64;
const double kCoarseMargin = 0.2;  // 粗い探索では、しきい値をこれだけ甘くする
const double kFineMargin = 0.1;
const double kMinStageThreshold = 0.05;
const double kMinVarRetention = 0.25;  // ぼかしたあとに残っていてほしい分散の割合

struct NeedleStats {
  uint64_t sum[3];
  uint64_t sumsq[3];
};

// 明るさを求めるときの、B・G・R の重み（合計 256）。
struct Weights {
  uint32_t w[3];
};

// 探す画像を、間引いた点の集まりにしたもの。
// 大きさ f の四角の平均を、間隔 s で、横 n 個 × 縦 m 個とる。
struct Level {
  int f = 1;
  int s = 1;
  int n = 0;
  int m = 0;
  std::vector<float> taps;  // 平均を引いた値
  double var = 0.0;
  int count() const { return n * m; }
};

struct Candidate {
  int x;
  int y;
  double score;
  int rank;  // score を 1/1000 単位に丸めたもの。並べ替えに使う
};

// 探索のたびに確保し直すと遅いので、作業用の領域は使い回す。
// （そのため、find_template は複数のスレッドから同時には呼べない。）
struct Buffers {
  std::vector<uint32_t> gray_integral;
  std::vector<uint32_t> channel_integral[3];
  std::vector<float> box;
  std::vector<float> acc;
  std::vector<double> p1;
  std::vector<double> p2;
};

Buffers& buffers() {
  static Buffers b;
  return b;
}

// 積分画像（左上からの累計）から、四角の中の合計を求める。
// 累計は 32 ビットであふれうるが、符号なしの引き算なので、四角の中の合計が
// 32 ビットに収まるかぎり正しい値になる。
inline uint32_t rect_sum(const uint32_t* integral, int stride, int x, int y, int w, int h) {
  const uint32_t* a = integral + static_cast<size_t>(y) * stride + x;
  const uint32_t* b = a + static_cast<size_t>(h) * stride;
  return b[w] - a[w] - b[0] + a[0];
}

inline uint32_t gray_of(const uint8_t* p, const Weights& wt) {
  return (p[0] * wt.w[0] + p[1] * wt.w[1] + p[2] * wt.w[2] + 128) >> 8;
}

NeedleStats needle_stats(const Image& needle) {
  NeedleStats ns = {{0, 0, 0}, {0, 0, 0}};
  for (int y = 0; y < needle.height; y++) {
    const uint8_t* p = needle.row(y);
    for (int x = 0; x < needle.width; x++) {
      for (int c = 0; c < 3; c++) {
        uint32_t v = p[x * 4 + c];
        ns.sum[c] += v;
        ns.sumsq[c] += v * v;
      }
    }
  }
  return ns;
}

double gray_variance(const Image& image, const Weights& wt) {
  uint64_t sum = 0, sumsq = 0;
  for (int y = 0; y < image.height; y++) {
    const uint8_t* p = image.row(y);
    for (int x = 0; x < image.width; x++) {
      uint32_t g = gray_of(p + x * 4, wt);
      sum += g;
      sumsq += g * g;
    }
  }
  const double n = static_cast<double>(image.width) * image.height;
  const double mean = sum / n;
  return std::max(0.0, sumsq / n - mean * mean);
}

// 明るさの求め方を決める。ふつうは輝度（人の目の感じる明るさ）を使う。
// 赤い地に同じ明るさの緑の文字、のように輝度では模様が消えてしまう画像では、
// 模様がいちばんはっきり出る色をそのまま使う。
Weights choose_weights(const Image& needle, const NeedleStats& ns, double& gray_var) {
  const Weights luma = {{29, 150, 77}};
  const double n = static_cast<double>(needle.width) * needle.height;
  double channel_var[3];
  double mean_var = 0.0;
  for (int c = 0; c < 3; c++) {
    const double mean = ns.sum[c] / n;
    channel_var[c] = std::max(0.0, ns.sumsq[c] / n - mean * mean);
    mean_var += channel_var[c] / 3.0;
  }

  gray_var = gray_variance(needle, luma);
  if (gray_var * 4.0 >= mean_var) return luma;

  int best = 0;
  for (int c = 1; c < 3; c++) {
    if (channel_var[c] > channel_var[best]) best = c;
  }
  Weights single = {{0, 0, 0}};
  single.w[best] = 256;
  gray_var = channel_var[best];
  return single;
}

// 明るさの積分画像を作る。大きさは (幅 + 1) × (高さ + 1)。
void gray_integral(const Image& image, const Weights& wt, std::vector<uint32_t>& out) {
  const int w = image.width, h = image.height, stride = w + 1;
  out.resize(static_cast<size_t>(stride) * (h + 1));
  std::fill_n(out.begin(), stride, 0u);
  for (int y = 0; y < h; y++) {
    const uint8_t* p = image.row(y);
    const uint32_t* up = out.data() + static_cast<size_t>(y) * stride;
    uint32_t* cur = out.data() + static_cast<size_t>(y + 1) * stride;
    uint32_t run = 0;
    cur[0] = 0;
    for (int x = 0; x < w; x++) {
      run += gray_of(p + x * 4, wt);
      cur[x + 1] = up[x + 1] + run;
    }
  }
}

// 探される側の画像について、明るさと、B・G・R それぞれの積分画像を作る。
void prepare_haystack(const Image& hay, const Weights& wt, Buffers& b) {
  const int w = hay.width, h = hay.height, stride = w + 1;
  const size_t size = static_cast<size_t>(stride) * (h + 1);
  b.gray_integral.resize(size);
  std::fill_n(b.gray_integral.begin(), stride, 0u);
  for (int c = 0; c < 3; c++) {
    b.channel_integral[c].resize(size);
    std::fill_n(b.channel_integral[c].begin(), stride, 0u);
  }

  for (int y = 0; y < h; y++) {
    const uint8_t* p = hay.row(y);
    const size_t up = static_cast<size_t>(y) * stride;
    const size_t cur = up + stride;
    uint32_t* g = b.gray_integral.data();
    uint32_t* c0 = b.channel_integral[0].data();
    uint32_t* c1 = b.channel_integral[1].data();
    uint32_t* c2 = b.channel_integral[2].data();
    g[cur] = c0[cur] = c1[cur] = c2[cur] = 0;
    uint32_t rg = 0, r0 = 0, r1 = 0, r2 = 0;
    for (int x = 0; x < w; x++) {
      const uint8_t* q = p + x * 4;
      rg += gray_of(q, wt);
      r0 += q[0];
      r1 += q[1];
      r2 += q[2];
      g[cur + x + 1] = g[up + x + 1] + rg;
      c0[cur + x + 1] = c0[up + x + 1] + r0;
      c1[cur + x + 1] = c1[up + x + 1] + r1;
      c2[cur + x + 1] = c2[up + x + 1] + r2;
    }
  }
}

int tap_count(int w, int h, int f, int s) {
  return ((w - f) / s + 1) * ((h - f) / s + 1);
}

Level make_level(const std::vector<uint32_t>& needle_integral, int w, int h, int f, int s) {
  Level level;
  level.f = f;
  level.s = s;
  level.n = (w - f) / s + 1;
  level.m = (h - f) / s + 1;
  const int count = level.count();
  const double inv = 1.0 / (static_cast<double>(f) * f);

  std::vector<double> values(static_cast<size_t>(count));
  double sum = 0.0, sumsq = 0.0;
  for (int j = 0; j < level.m; j++) {
    for (int i = 0; i < level.n; i++) {
      const double v = rect_sum(needle_integral.data(), w + 1, i * s, j * s, f, f) * inv;
      values[static_cast<size_t>(j) * level.n + i] = v;
      sum += v;
      sumsq += v * v;
    }
  }
  const double mean = sum / count;
  level.var = std::max(0.0, sumsq / count - mean * mean);
  level.taps.resize(static_cast<size_t>(count));
  for (int k = 0; k < count; k++) level.taps[k] = static_cast<float>(values[k] - mean);
  return level;
}

// 粗い探索での点のとり方を決める。四角は大きいほど、画像全体を少ない点で表せる。
// ただし、細かい文字や模様は、大きな四角でぼかすと消えてしまう。模様（分散）が
// 十分に残る範囲で、いちばん大きな四角を選ぶ。
Level choose_coarse_level(const std::vector<uint32_t>& needle_integral, int w, int h,
                          double gray_var) {
  int f = 1;
  for (int candidate = 64; candidate >= 2; candidate /= 2) {
    if (w < candidate || h < candidate) continue;
    if (tap_count(w, h, candidate, candidate) < kMinTaps) continue;
    const Level level = make_level(needle_integral, w, h, candidate, candidate);
    if (level.var >= kMinVarRetention * gray_var) {
      f = candidate;
      break;
    }
  }
  int s = f;
  while (tap_count(w, h, f, s) > kMaxTaps) s++;
  return make_level(needle_integral, w, h, f, s);
}

inline double shape_score(double cov, double var_t, double var_c) {
  return (2.0 * cov + kVarFloor) / (var_t + var_c + kVarFloor);
}

// 色の一致度を、積分画像から求める。
struct ColorCheck {
  const uint32_t* integral[3];
  int stride;
  int w;
  int h;
  double pixels;
  double needle_sum[3];

  double at(int x, int y) const {
    double d2 = 0.0;
    for (int c = 0; c < 3; c++) {
      const double sum = rect_sum(integral[c], stride, x, y, w, h);
      const double d = (sum - needle_sum[c]) / pixels;
      d2 += d * d;
    }
    const double score = 1.0 - (d2 / 3.0) / (kColorRange * kColorRange);
    return score > 0.0 ? score : 0.0;
  }
};

bool better(const Candidate& a, const Candidate& b) {
  if (a.rank != b.rank) return a.rank > b.rank;
  if (a.y != b.y) return a.y < b.y;
  return a.x < b.x;
}

// 一致度の高い順（同じなら上、左の順）に並べ、limit 個までに絞る。
void keep_best(std::vector<Candidate>& candidates, size_t limit) {
  if (candidates.size() > limit) {
    std::partial_sort(candidates.begin(), candidates.begin() + limit, candidates.end(), better);
    candidates.resize(limit);
  } else {
    std::sort(candidates.begin(), candidates.end(), better);
  }
}

inline Candidate make_candidate(int x, int y, double score) {
  return Candidate{x, y, score, static_cast<int>(std::lround(score * 1000.0))};
}

// 粗い探索。すべての場所について、level の点だけで一致度を求める。
void coarse_stage(Buffers& b, int W, int H, int w, int h, const Level& level,
                  const ColorCheck& color, double threshold, std::vector<Candidate>& out) {
  const int f = level.f, s = level.s, n = level.n, m = level.m;
  const int count = level.count();
  const int stride = W + 1;

  // 大きさ f の四角の平均を、すべての位置について求めておく。
  const int BW = W - f + 1, BH = H - f + 1;
  b.box.resize(static_cast<size_t>(BW) * BH);
  {
    const float inv = 1.0f / static_cast<float>(f * f);
    const uint32_t* integral = b.gray_integral.data();
    for (int y = 0; y < BH; y++) {
      const uint32_t* r0 = integral + static_cast<size_t>(y) * stride;
      const uint32_t* r1 = r0 + static_cast<size_t>(f) * stride;
      float* dst = b.box.data() + static_cast<size_t>(y) * BW;
      for (int x = 0; x < BW; x++) {
        dst[x] = static_cast<float>(static_cast<int32_t>(r1[x + f] - r0[x + f] - r1[x] + r0[x])) * inv;
      }
    }
  }

  // 間隔 s でとった点の合計と 2 乗の合計を、どの場所についてもすぐ求められるように、
  // 「間隔 s の積分画像」を作る。上と左に s ずつ、0 の余白を置く。
  const int PW = BW + s, PH = BH + s;
  b.p1.resize(static_cast<size_t>(PW) * PH);
  b.p2.resize(static_cast<size_t>(PW) * PH);
  std::fill_n(b.p1.begin(), static_cast<size_t>(PW) * s, 0.0);
  std::fill_n(b.p2.begin(), static_cast<size_t>(PW) * s, 0.0);
  for (int y = 0; y < BH; y++) {
    const float* src = b.box.data() + static_cast<size_t>(y) * BW;
    const double* u1 = b.p1.data() + static_cast<size_t>(y) * PW;
    const double* u2 = b.p2.data() + static_cast<size_t>(y) * PW;
    double* d1 = b.p1.data() + static_cast<size_t>(y + s) * PW;
    double* d2 = b.p2.data() + static_cast<size_t>(y + s) * PW;
    for (int x = 0; x < s; x++) d1[x] = d2[x] = 0.0;
    for (int x = 0; x < BW; x++) {
      const double v = src[x];
      d1[x + s] = v + u1[x + s] + d1[x] - u1[x];
      d2[x + s] = v * v + u2[x + s] + d2[x] - u2[x];
    }
  }

  const int NX = W - w + 1, NY = H - h + 1;
  b.acc.resize(static_cast<size_t>(NX));
  const double inv_count = 1.0 / count;
  const double var_t = level.var;

  // 形の一致度が threshold 以上になるには、共分散がこの値以上でなければならない。
  // 足りない場所は、分散を求めるまでもなく捨てられる。
  const double cov_min = (threshold * (var_t + kVarFloor) - kVarFloor) / 2.0;
  const float acc_min = static_cast<float>(cov_min * count - (std::fabs(cov_min * count) * 1e-3 + 1.0));

  const int xb = n * s, yb = m * s;
  for (int y = 0; y < NY; y++) {
    // 点ごとに、横 1 行ぶんの場所をまとめて計算する（コンパイラが SIMD 命令にする）。
    float* __restrict acc = b.acc.data();
    std::fill_n(acc, NX, 0.0f);
    for (int j = 0; j < m; j++) {
      const float* box_row = b.box.data() + static_cast<size_t>(y + j * s) * BW;
      for (int i = 0; i < n; i++) {
        const float tap = level.taps[static_cast<size_t>(j) * n + i];
        const float* __restrict src = box_row + i * s;
        for (int x = 0; x < NX; x++) acc[x] += tap * src[x];
      }
    }

    const double* p1a = b.p1.data() + static_cast<size_t>(y) * PW;
    const double* p1b = b.p1.data() + static_cast<size_t>(y + yb) * PW;
    const double* p2a = b.p2.data() + static_cast<size_t>(y) * PW;
    const double* p2b = b.p2.data() + static_cast<size_t>(y + yb) * PW;
    for (int x = 0; x < NX; x++) {
      if (acc[x] < acc_min) continue;
      const double sum1 = p1b[x + xb] - p1a[x + xb] - p1b[x] + p1a[x];
      const double sum2 = p2b[x + xb] - p2a[x + xb] - p2b[x] + p2a[x];
      const double mean = sum1 * inv_count;
      const double var_c = std::max(0.0, sum2 * inv_count - mean * mean);
      const double shape = shape_score(acc[x] * inv_count, var_t, var_c);
      if (shape < threshold) continue;
      const double score = shape * color.at(x, y);
      if (score >= threshold) out.push_back(make_candidate(x, y, score));
    }
  }
}

// 1 つの場所について、level の点だけで一致度を求める。
double sparse_score(const uint32_t* integral, int stride, int x, int y, const Level& level) {
  const int f = level.f, s = level.s;
  const double inv = 1.0 / (static_cast<double>(f) * f);
  double sum1 = 0.0, sum2 = 0.0, cross = 0.0;
  for (int j = 0; j < level.m; j++) {
    const uint32_t* r0 = integral + static_cast<size_t>(y + j * s) * stride + x;
    const uint32_t* r1 = r0 + static_cast<size_t>(f) * stride;
    const float* taps = level.taps.data() + static_cast<size_t>(j) * level.n;
    for (int i = 0; i < level.n; i++) {
      const int xx = i * s;
      const double v = static_cast<double>(r1[xx + f] - r0[xx + f] - r1[xx] + r0[xx]) * inv;
      sum1 += v;
      sum2 += v * v;
      cross += taps[i] * v;
    }
  }
  const int count = level.count();
  const double mean = sum1 / count;
  const double var_c = std::max(0.0, sum2 / count - mean * mean);
  return shape_score(cross / count, level.var, var_c);
}

// 一致度を、すべての画素と色を使って求める。
//
// 分散などは整数のまま計算する。完全に一致する場所では、分子と分母が同じ整数に
// なるので、結果がちょうど 1.0 になる（128 ビットの整数は、あふれを避けるため）。
double exact_score(const Image& hay, const Image& needle, const NeedleStats& ns, int x0, int y0) {
  const int w = needle.width, h = needle.height;
  uint64_t sum[3] = {0, 0, 0}, sumsq[3] = {0, 0, 0}, cross[3] = {0, 0, 0};
  for (int y = 0; y < h; y++) {
    const uint8_t* hp = hay.row(y0 + y) + static_cast<size_t>(x0) * 4;
    const uint8_t* np = needle.row(y);
    // 1 行ぶんは 32 ビットに収まる（幅は 32767 以下）。
    uint32_t rs[3] = {0, 0, 0}, rq[3] = {0, 0, 0}, rc[3] = {0, 0, 0};
    for (int x = 0; x < w; x++) {
      for (int c = 0; c < 3; c++) {
        const uint32_t a = hp[x * 4 + c];
        const uint32_t t = np[x * 4 + c];
        rs[c] += a;
        rq[c] += a * a;
        rc[c] += a * t;
      }
    }
    for (int c = 0; c < 3; c++) {
      sum[c] += rs[c];
      sumsq[c] += rq[c];
      cross[c] += rc[c];
    }
  }

  typedef __int128 i128;
  const i128 n = static_cast<i128>(w) * h;
  const double pixels = static_cast<double>(w) * h;
  i128 var_t = 0, var_c = 0, cov = 0;  // どれも「画素数の 2 乗」倍した値
  double d2 = 0.0;
  for (int c = 0; c < 3; c++) {
    var_t += n * ns.sumsq[c] - static_cast<i128>(ns.sum[c]) * ns.sum[c];
    var_c += n * sumsq[c] - static_cast<i128>(sum[c]) * sum[c];
    cov += n * cross[c] - static_cast<i128>(ns.sum[c]) * sum[c];
    const double d = (static_cast<double>(ns.sum[c]) - static_cast<double>(sum[c])) / pixels;
    d2 += d * d;
  }
  const double floor = 3.0 * pixels * pixels * kVarFloor;
  const double shape = (2.0 * static_cast<double>(cov) + floor) /
                       (static_cast<double>(var_t) + static_cast<double>(var_c) + floor);
  const double color = std::max(0.0, 1.0 - (d2 / 3.0) / (kColorRange * kColorRange));
  return shape * color;
}

}  // namespace

MatchResult find_template(const Image& hay, const Image& needle, double threshold) {
  MatchResult result;
  const int W = hay.width, H = hay.height, w = needle.width, h = needle.height;
  if (w <= 0 || h <= 0 || w > W || h > H) return result;

  const NeedleStats ns = needle_stats(needle);
  double gray_var = 0.0;
  const Weights weights = choose_weights(needle, ns, gray_var);

  std::vector<uint32_t> needle_integral;
  gray_integral(needle, weights, needle_integral);

  Buffers& b = buffers();
  prepare_haystack(hay, weights, b);

  ColorCheck color;
  for (int c = 0; c < 3; c++) {
    color.integral[c] = b.channel_integral[c].data();
    color.needle_sum[c] = static_cast<double>(ns.sum[c]);
  }
  color.stride = W + 1;
  color.w = w;
  color.h = h;
  color.pixels = static_cast<double>(w) * h;

  // 1. 粗い探索
  const Level coarse = choose_coarse_level(needle_integral, w, h, gray_var);
  std::vector<Candidate> candidates;
  coarse_stage(b, W, H, w, h, coarse, color,
               std::max(threshold - kCoarseMargin, kMinStageThreshold), candidates);
  keep_best(candidates, kMaxCoarseCandidates);

  // 2. 絞り込み（大きな画像のときだけ）
  const bool coarse_is_full = coarse.f == 1 && coarse.s == 1;
  if (!coarse_is_full && static_cast<int64_t>(w) * h > kFineStageMinPixels) {
    const int f = std::max(1, coarse.f / 4);
    int s = f;
    while (tap_count(w, h, f, s) > kMaxFineTaps) s++;
    const Level fine = make_level(needle_integral, w, h, f, s);
    const double fine_threshold = std::max(threshold - kFineMargin, kMinStageThreshold);

    std::vector<Candidate> kept;
    for (const Candidate& c : candidates) {
      const double shape = sparse_score(b.gray_integral.data(), W + 1, c.x, c.y, fine);
      if (shape < fine_threshold) continue;
      const double score = shape * color.at(c.x, c.y);
      if (score >= fine_threshold) kept.push_back(make_candidate(c.x, c.y, score));
    }
    keep_best(kept, kMaxFineCandidates);
    candidates.swap(kept);
  }

  // 3. 判定。候補は一致度の高い順（同じなら上、左の順）に並んでいる。
  for (const Candidate& c : candidates) {
    const double score = exact_score(hay, needle, ns, c.x, c.y);
    if (!result.has_score || score > result.score) {
      result.has_score = true;
      result.score = score;
      result.x = c.x;
      result.y = c.y;
    }
    if (result.score >= 1.0 - 1e-9) break;  // 完全に一致した。これ以上のものは無い
  }
  result.found = result.has_score && result.score >= threshold;
  return result;
}

double match_score_at(const Image& hay, const Image& needle, int x, int y) {
  if (needle.width <= 0 || needle.height <= 0 || x < 0 || y < 0 ||
      x + needle.width > hay.width || y + needle.height > hay.height) {
    return 0.0;
  }
  return exact_score(hay, needle, needle_stats(needle), x, y);
}
