// 画像の読み書き。Windows に標準で入っている WIC（Windows Imaging Component）を使う。
// WIC は COM の部品として提供されており、実体の windowscodecs.dll は実行時に読み込まれる。

#include "image.h"

#include <windows.h>
#include <wincodec.h>

#include <cstdio>

#include "util.h"

namespace {

// COM のオブジェクトを、スコープを抜けるときに解放する。
template <class T>
class ComPtr {
 public:
  ComPtr() = default;
  ComPtr(const ComPtr&) = delete;
  ComPtr& operator=(const ComPtr&) = delete;
  ~ComPtr() {
    if (p_) p_->Release();
  }
  T** operator&() { return &p_; }
  T* operator->() const { return p_; }
  T* get() const { return p_; }

 private:
  T* p_ = nullptr;
};

// 画素数の上限。32 ビットの計算があふれない範囲にする。
const UINT kMaxSide = 32767;
const uint64_t kMaxPixels = 1ull << 26;  // 約 6700 万画素（8K の画面の 2 倍）

IWICImagingFactory* wic_factory() {
  static IWICImagingFactory* factory = nullptr;
  static bool tried = false;
  if (!tried) {
    tried = true;
    ensure_com();
    CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                     IID_PPV_ARGS(&factory));
  }
  return factory;
}

std::string hresult_message(HRESULT hr) {
  switch (static_cast<unsigned long>(hr)) {
    case 0x80070002ul:
    case 0x80070003ul:
      return "ファイルがありません";
    case 0x80070005ul:
      return "アクセスが拒否されました";
    case 0x80070020ul:
      return "ほかのプログラムが使用中です";
    case 0x88982F50ul:  // WINCODEC_ERR_COMPONENTNOTFOUND
    case 0x88982F07ul:  // WINCODEC_ERR_UNKNOWNIMAGEFORMAT
      return "対応していない画像形式です";
    case 0x88982F60ul:  // WINCODEC_ERR_BADIMAGE
    case 0x88982F61ul:  // WINCODEC_ERR_BADHEADER
      return "画像ファイルが壊れています";
  }
  char buf[48];
  snprintf(buf, sizeof(buf), "エラーコード 0x%08lX", static_cast<unsigned long>(hr));
  return buf;
}

bool fail(HRESULT hr, std::string& error) {
  error = hresult_message(hr);
  return false;
}

}  // namespace

bool load_image(const std::wstring& path, Image& out, std::string& error) {
  IWICImagingFactory* factory = wic_factory();
  if (!factory) {
    error = "画像を扱う機能（WIC）を初期化できません";
    return false;
  }

  ComPtr<IWICBitmapDecoder> decoder;
  HRESULT hr = factory->CreateDecoderFromFilename(
      path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder);
  if (FAILED(hr)) return fail(hr, error);

  ComPtr<IWICBitmapFrameDecode> frame;
  hr = decoder->GetFrame(0, &frame);
  if (FAILED(hr)) return fail(hr, error);

  // どんな形式の画像も、1 画素 4 バイトの BGRA に変換して受け取る。
  ComPtr<IWICFormatConverter> converter;
  hr = factory->CreateFormatConverter(&converter);
  if (FAILED(hr)) return fail(hr, error);
  hr = converter->Initialize(frame.get(), GUID_WICPixelFormat32bppBGRA,
                             WICBitmapDitherTypeNone, nullptr, 0.0,
                             WICBitmapPaletteTypeCustom);
  if (FAILED(hr)) return fail(hr, error);

  UINT w = 0, h = 0;
  hr = converter->GetSize(&w, &h);
  if (FAILED(hr)) return fail(hr, error);
  if (w == 0 || h == 0 || w > kMaxSide || h > kMaxSide ||
      static_cast<uint64_t>(w) * h > kMaxPixels) {
    error = "画像が大きすぎます";
    return false;
  }

  out.width = static_cast<int>(w);
  out.height = static_cast<int>(h);
  out.pixels.resize(static_cast<size_t>(w) * h * 4);
  hr = converter->CopyPixels(nullptr, w * 4, static_cast<UINT>(out.pixels.size()),
                             out.pixels.data());
  if (FAILED(hr)) return fail(hr, error);
  return true;
}

bool save_png(const std::wstring& path, const Image& image, std::string& error) {
  if (image.width <= 0 || image.height <= 0) {
    error = "保存する画像がありません";
    return false;
  }
  IWICImagingFactory* factory = wic_factory();
  if (!factory) {
    error = "画像を扱う機能（WIC）を初期化できません";
    return false;
  }

  ComPtr<IWICStream> stream;
  HRESULT hr = factory->CreateStream(&stream);
  if (FAILED(hr)) return fail(hr, error);
  hr = stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE);
  if (FAILED(hr)) return fail(hr, error);

  ComPtr<IWICBitmapEncoder> encoder;
  hr = factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder);
  if (FAILED(hr)) return fail(hr, error);
  hr = encoder->Initialize(stream.get(), WICBitmapEncoderNoCache);
  if (FAILED(hr)) return fail(hr, error);

  ComPtr<IWICBitmapFrameEncode> frame;
  hr = encoder->CreateNewFrame(&frame, nullptr);
  if (FAILED(hr)) return fail(hr, error);
  hr = frame->Initialize(nullptr);
  if (FAILED(hr)) return fail(hr, error);

  const UINT w = static_cast<UINT>(image.width);
  const UINT h = static_cast<UINT>(image.height);
  hr = frame->SetSize(w, h);
  if (FAILED(hr)) return fail(hr, error);

  // A（不透明度）は不定なので、B、G、R の 3 バイトだけを保存する。
  WICPixelFormatGUID format = GUID_WICPixelFormat24bppBGR;
  hr = frame->SetPixelFormat(&format);
  if (FAILED(hr)) return fail(hr, error);
  if (!IsEqualGUID(format, GUID_WICPixelFormat24bppBGR)) {
    error = "PNG の画素形式を設定できません";
    return false;
  }

  const UINT stride = w * 3;
  std::vector<uint8_t> bgr(static_cast<size_t>(stride) * h);
  for (UINT y = 0; y < h; y++) {
    const uint8_t* src = image.row(static_cast<int>(y));
    uint8_t* dst = bgr.data() + static_cast<size_t>(y) * stride;
    for (UINT x = 0; x < w; x++) {
      dst[x * 3 + 0] = src[x * 4 + 0];
      dst[x * 3 + 1] = src[x * 4 + 1];
      dst[x * 3 + 2] = src[x * 4 + 2];
    }
  }
  hr = frame->WritePixels(h, stride, static_cast<UINT>(bgr.size()), bgr.data());
  if (FAILED(hr)) return fail(hr, error);

  hr = frame->Commit();
  if (FAILED(hr)) return fail(hr, error);
  hr = encoder->Commit();
  if (FAILED(hr)) return fail(hr, error);
  return true;
}
