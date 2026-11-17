// Pikulix: mruby のスクリプトを読んで実行するホスト。
//
//   pikulix.exe script.rb
//   pikulix.exe --screen-image 画像 script.rb   （テストモード）
//
// 終了コード: 0 成功、1 スクリプトのエラー、2 起動できなかった、3 緊急停止。
// スクリプトが exit を呼んだときは、その値。

#include <windows.h>

#include <cstdio>
#include <cwchar>
#include <string>

#include <mruby.h>
#include <mruby/compile.h>
#include <mruby/variable.h>

#include "api.h"
#include "app.h"
#include "console.h"
#include "image.h"
#include "input.h"
#include "util.h"

// src/prelude.rb の中身。ビルドのときに Rakefile が生成する（kPreludeSource）。
#include "prelude.inc"

App g_app;

namespace {

const int kExitScriptError = 1;
const int kExitStartupError = 2;
const int kExitEmergencyStop = 3;

bool read_file(const wchar_t* path, std::string& out) {
  FILE* fp = _wfopen(path, L"rb");
  if (!fp) return false;
  char buf[4096];
  size_t n;
  while ((n = fread(buf, 1, sizeof(buf), fp)) > 0) out.append(buf, n);
  bool ok = !ferror(fp);
  fclose(fp);
  return ok;
}

// パスからファイル名を除いて、ディレクトリにする。
std::wstring directory_of(const std::wstring& path) {
  const size_t slash = path.find_last_of(L"\\/");
  return slash == std::wstring::npos ? std::wstring(L".") : path.substr(0, slash);
}

std::wstring full_path(const wchar_t* path) {
  wchar_t buf[32768];
  const DWORD len = GetFullPathNameW(path, 32768, buf, nullptr);
  return (len == 0 || len >= 32768) ? std::wstring(path) : std::wstring(buf, len);
}

std::wstring exe_path() {
  wchar_t buf[32768];
  const DWORD len = GetModuleFileNameW(nullptr, buf, 32768);
  return std::wstring(buf, len);
}

// --- 緊急停止 ---
//
// Alt+Shift+C を、どのウィンドウが前面でも効くホットキーとして登録し、専用の
// スレッドで待つ。押されたら、押したままのキーとマウスボタンを離して、プロセスを
// 即座に終了する。スクリプトがどんな状態でも（無限ループの中でも）止まる。

HANDLE g_hotkey_ready = nullptr;

void emergency_stop() {
  input::stop_and_release();
  // メインのスレッドが C ランタイムの出力の途中かもしれないので、C ランタイムは使わない。
  console::write_error_raw("\n[pikulix] 緊急停止しました（Alt+Shift+C）\n");
  console::restore_window(true);
  console::restore_code_page();
  TerminateProcess(GetCurrentProcess(), kExitEmergencyStop);
}

DWORD WINAPI hotkey_thread(LPVOID) {
  // MOD_NOREPEAT は、押しっぱなしにしても 1 回だけ知らせる指定。
  g_app.hotkey_ok = RegisterHotKey(nullptr, 1, MOD_ALT | MOD_SHIFT | MOD_NOREPEAT, 'C') != 0;
  SetEvent(g_hotkey_ready);
  if (!g_app.hotkey_ok) return 0;

  MSG msg;
  while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
    if (msg.message == WM_HOTKEY) emergency_stop();
  }
  return 0;
}

void start_hotkey_thread() {
  g_hotkey_ready = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  HANDLE thread = CreateThread(nullptr, 0, hotkey_thread, nullptr, 0, nullptr);
  if (!thread) return;
  WaitForSingleObject(g_hotkey_ready, 5000);  // 登録の結果が出るのを待つ
  CloseHandle(thread);
}

// コンソールで Ctrl+C が押された、またはコンソールが閉じられたときに呼ばれる。
// 終了そのものは Windows の標準の動作に任せ、後始末だけを行う。
BOOL WINAPI on_console_close(DWORD) {
  input::stop_and_release();
  console::restore_code_page();
  return FALSE;
}

// --- スクリプトの実行 ---

// Ruby のソースを実行する。成功なら 0、失敗なら終了コードを返す。
// filename は、エラー時に「ファイル名:行番号」として表示される。
int load_source(mrb_state* mrb, const char* source, size_t len, const char* filename) {
  mrb_ccontext* cxt = mrb_ccontext_new(mrb);
  mrb_ccontext_filename(mrb, cxt, filename);
  mrb_load_nstring_cxt(mrb, source, len, cxt);
  bool syntax_error = cxt->parser_nerr > 0;
  mrb_ccontext_free(mrb, cxt);

  if (!mrb->exc) return 0;

  // exit は、SystemExit という例外として届く（src/prelude.rb）。
  mrb_value exc = mrb_obj_value(mrb->exc);
  if (mrb_class_defined(mrb, "SystemExit") &&
      mrb_obj_is_kind_of(mrb, exc, mrb_class_get(mrb, "SystemExit"))) {
    mrb_value status = mrb_iv_get(mrb, exc, mrb_intern_lit(mrb, "@status"));
    mrb->exc = nullptr;
    return mrb_integer_p(status) ? static_cast<int>(mrb_integer(status)) : 0;
  }

  // 文法エラーは、パーサが「ファイル名:行番号: 内容」をすでに出力している。
  if (!syntax_error) {
    // エラーの場所として利用者のスクリプトの行が表示されるように、呼び出し履歴を
    // 整える（src/prelude.rb の _clean_backtrace）。整えるのに失敗しても無視する。
    struct RObject* error = mrb->exc;
    mrb->exc = nullptr;
    if (mrb_class_defined(mrb, "Pikulix")) {
      mrb_funcall(mrb, mrb_obj_value(mrb_module_get(mrb, "Pikulix")), "_clean_backtrace", 1,
                  mrb_obj_value(error));
    }
    mrb->exc = error;
    mrb_print_error(mrb);
  }
  mrb->exc = nullptr;
  return kExitScriptError;
}

int run_script(const wchar_t* path) {
  std::string path_utf8 = wide_to_utf8(path);

  std::string source;
  if (!read_file(path, source)) {
    fprintf(stderr, "pikulix: スクリプトを読めません: %s\n", path_utf8.c_str());
    return kExitStartupError;
  }

  // メモ帳などが先頭に付ける UTF-8 の BOM を読み飛ばす。
  size_t offset = 0;
  if (source.compare(0, 3, "\xEF\xBB\xBF") == 0) offset = 3;

  mrb_state* mrb = mrb_open();
  if (MRB_OPEN_FAILURE(mrb)) {
    fprintf(stderr, "pikulix: mruby を初期化できません\n");
    if (mrb) mrb_close(mrb);
    return kExitStartupError;
  }

  define_api(mrb);

  int status = 0;
  if (load_source(mrb, reinterpret_cast<const char*>(kPreludeSource), sizeof(kPreludeSource),
                  "(pikulix)") != 0) {
    fprintf(stderr, "pikulix: 組み込みメソッドを読み込めません\n");
    status = kExitStartupError;
  } else {
    status = load_source(mrb, source.data() + offset, source.size() - offset, path_utf8.c_str());
  }
  mrb_close(mrb);
  return status;
}

int usage() {
  fprintf(stderr,
          "使い方: pikulix.exe script.rb\n"
          "\n"
          "  --screen-image 画像   テストモード。画面の代わりに画像ファイルを探し、\n"
          "                        マウスやキーボードの操作は送らない（ログだけ出す）。\n");
  return kExitStartupError;
}

}  // namespace

// wmain は、引数を UTF-16 で受け取る main。リンク時に -municode が要る。
int wmain(int argc, wchar_t** argv) {
  console::use_utf8();

  // 標準出力をバッファしない。ファイルへリダイレクトしたときにも、標準エラーと
  // 順序が入れ替わらず、途中で強制終了されても出力が失われない。
  setvbuf(stdout, nullptr, _IONBF, 0);

  const wchar_t* screen_image = nullptr;
  int i = 1;
  while (i < argc && wcsncmp(argv[i], L"--", 2) == 0) {
    if (wcscmp(argv[i], L"--screen-image") == 0 && i + 1 < argc) {
      screen_image = argv[i + 1];
      i += 2;
    } else {
      int status = usage();
      console::restore_code_page();
      return status;
    }
  }
  if (argc - i != 1) {
    int status = usage();
    console::restore_code_page();
    return status;
  }
  const wchar_t* script = argv[i];

  g_app.script_dir = directory_of(full_path(script));
  g_app.exe_dir = directory_of(exe_path());

  int status = 0;
  if (screen_image) {
    g_app.dry_run = true;
    std::string error;
    if (!load_image(full_path(screen_image), g_app.fake_screen, error)) {
      fprintf(stderr, "pikulix: 画像を読めません: %s（%s）\n",
              wide_to_utf8(screen_image).c_str(), error.c_str());
      status = kExitStartupError;
    }
  } else {
    start_hotkey_thread();
  }

  if (status == 0) {
    SetConsoleCtrlHandler(on_console_close, TRUE);
    status = run_script(script);
  }

  input::release_all();
  console::restore_window(false);
  console::restore_code_page();
  return status;
}
