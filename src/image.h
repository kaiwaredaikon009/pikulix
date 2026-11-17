// 画像と、その読み書き。

#pragma once

#include <cstdint>
#include <string>
#include <vector>

// 1 画素 4 バイト（B、G、R、A の順）で、上の行から順に並ぶ。
// A（不透明度）は使わない。画面キャプチャでは値が不定である。
struct Image {
  int width = 0;
  int height = 0;
  std::vector<uint8_t> pixels;

  const uint8_t* row(int y) const {
    return pixels.data() + static_cast<size_t>(y) * static_cast<size_t>(width) * 4;
  }
  uint8_t* row(int y) {
    return pixels.data() + static_cast<size_t>(y) * static_cast<size_t>(width) * 4;
  }
};

// 画像ファイル（PNG のほか、Windows が読める形式）を読む。
// 失敗したら false を返し、error に理由を入れる。
bool load_image(const std::wstring& path, Image& out, std::string& error);

// PNG として保存する。
bool save_png(const std::wstring& path, const Image& image, std::string& error);
