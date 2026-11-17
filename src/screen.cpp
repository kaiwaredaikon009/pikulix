// 画面キャプチャ。GDI（Windows の昔からある描画の仕組み）の BitBlt で写し取る。
//
// exe のマニフェストで Per-Monitor V2 の DPI 対応を宣言しているので、ここで扱う
// 大きさと座標は、表示倍率にかかわらず、実際の画素（物理ピクセル）が単位である。

#include "screen.h"

#include <windows.h>

#include <cstring>

#include "util.h"

bool capture_screen(Image& out, int& origin_x, int& origin_y, std::string& error) {
  const int x = 0, y = 0;
  const int w = GetSystemMetrics(SM_CXSCREEN);
  const int h = GetSystemMetrics(SM_CYSCREEN);
  if (w <= 0 || h <= 0) {
    error = "画面の大きさを取得できません";
    return false;
  }

  HDC screen = GetDC(nullptr);
  if (!screen) {
    error = "画面を取得できません";
    return false;
  }
  HDC memory = CreateCompatibleDC(screen);

  // 写し取り先。1 画素 4 バイトで、上の行から並ぶ（高さを負にするとそうなる）。
  BITMAPINFO info = {};
  info.bmiHeader.biSize = sizeof(info.bmiHeader);
  info.bmiHeader.biWidth = w;
  info.bmiHeader.biHeight = -h;
  info.bmiHeader.biPlanes = 1;
  info.bmiHeader.biBitCount = 32;
  info.bmiHeader.biCompression = BI_RGB;
  void* bits = nullptr;
  HBITMAP bitmap = memory ? CreateDIBSection(screen, &info, DIB_RGB_COLORS, &bits, nullptr, 0)
                          : nullptr;

  bool ok = false;
  if (bitmap && bits) {
    HGDIOBJ old = SelectObject(memory, bitmap);
    if (BitBlt(memory, 0, 0, w, h, screen, x, y, SRCCOPY | CAPTUREBLT)) {
      GdiFlush();
      out.width = w;
      out.height = h;
      out.pixels.resize(static_cast<size_t>(w) * h * 4);
      memcpy(out.pixels.data(), bits, out.pixels.size());
      ok = true;
    } else {
      // ロック画面や UAC の確認画面では、ここで失敗する。
      error = "画面を写し取れません: " + windows_error_message(GetLastError());
    }
    SelectObject(memory, old);
  } else {
    error = "画面を写し取るためのメモリを確保できません";
  }

  if (bitmap) DeleteObject(bitmap);
  if (memory) DeleteDC(memory);
  ReleaseDC(nullptr, screen);

  origin_x = x;
  origin_y = y;
  return ok;
}
