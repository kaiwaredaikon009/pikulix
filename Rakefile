# frozen_string_literal: true

# Pikulix のビルド。使い方は docs/ビルド手順.md を参照。
#
#   rake            pikulix.exe をビルドし、依存 DLL を確かめる
#   rake test       ビルドして、画面を使わないテストを実行する
#   rake bench      画像を探す処理の所要時間をはかる
#   rake sandbox    Windows サンドボックスで確かめるためのファイルを用意する
#   rake clean      ホストの生成物を消す（mruby のビルド結果は残す）
#   rake clobber    build/ をすべて消す

require "etc"
require "rake/clean"
require "rbconfig"

# 取り込む mruby。タグは書き換えられうるので、コミットも照合する。
MRUBY_REPO = "https://github.com/mruby/mruby.git"
MRUBY_TAG = "4.0.0"
MRUBY_COMMIT = "831da26b9021de0369d17b71b5667e2941a1a32d"

MRUBY_DIR = "vendor/mruby"
MRUBY_CONFIG = "mruby_build_config.rb"
MRUBY_BUILD_DIR = "build/mruby"
MRUBY_INCLUDE_DIR = "#{MRUBY_BUILD_DIR}/host/include"
MRUBY_LIB_DIR = "#{MRUBY_BUILD_DIR}/host/lib"
LIBMRUBY = "#{MRUBY_LIB_DIR}/libmruby.a"

OBJ_DIR = "build/obj"
GEN_DIR = "build/gen" # ビルドの途中で生成するソース
EXE = "build/pikulix.exe"
SANDBOX_DIR = "build/sandbox"

# mruby のビルドに使った定義。ホストも同じ定義でコンパイルする。
# mruby_build_config.rb の conf.defines と揃えること。
MRUBY_DEFINES = %w[-DMRB_UTF8_STRING].freeze

# -O3: 画像を探す処理（src/match.cpp）のループを、SIMD 命令（複数の値をまとめて
#      計算する CPU の命令）にしてもらうため。-O2 では、そうならないループがある。
CXXFLAGS = (%w[-std=c++17 -O3 -Wall -Wextra -DUNICODE -D_UNICODE] + MRUBY_DEFINES).freeze

# -static:   libstdc++ などを exe に取り込み、DLL に依存させない。
# -municode: エントリポイントを wmain にする。
# -s:        デバッグ用のシンボルを落として exe を小さくする。
LDFLAGS = %w[-static -municode -s].freeze
LIBS = %w[-lmruby -luser32 -lgdi32 -lshell32 -lole32 -lwindowscodecs -luuid -ladvapi32].freeze

# exe が依存してよい DLL。どれも Windows に標準で入っている。
# 新しい Win32 API を使って依存先が増えたら、標準の DLL であることを確かめて足す。
ALLOWED_DLLS = [
  /\Aapi-ms-win-crt-[a-z0-9-]+\.dll\z/i, # UCRT（Windows 10 以降に標準の C ランタイム）
  /\Akernel32\.dll\z/i,
  /\Auser32\.dll\z/i,  # ウィンドウ、マウスとキーボードの入力、クリップボード
  /\Agdi32\.dll\z/i,   # 画面キャプチャ
  /\Ashell32\.dll\z/i, # アプリの起動（run）
  /\Aole32\.dll\z/i,   # COM。画像の読み書き（WIC）を呼び出すのに使う
  /\Aadvapi32\.dll\z/i # 操作の相手が管理者として動いているかを調べる
].freeze

CLEAN.include(OBJ_DIR, GEN_DIR, EXE, SANDBOX_DIR)
CLOBBER.include("build")

# gcc を使えるようにする。PATH に無ければ、RubyInstaller 付属の MSYS2 を
# このプロセスの中だけで有効にする（`ridk enable` と同じ効果）。
def enable_toolchain
  return if @toolchain_enabled

  unless system("gcc", "--version", out: File::NULL, err: File::NULL)
    begin
      require "ruby_installer/runtime"
      RubyInstaller::Runtime.enable_msys_apps
    rescue LoadError, StandardError => e
      abort "gcc が見つかりません。docs/ビルド手順.md の準備を確認してください。(#{e.class}: #{e.message})"
    end
  end
  @toolchain_enabled = true
end

namespace :mruby do
  desc "mruby #{MRUBY_TAG} のソースを #{MRUBY_DIR} に取得する"
  task :fetch do
    unless Dir.exist?(MRUBY_DIR)
      sh "git", "-c", "advice.detachedHead=false", "clone", "--depth", "1",
         "--branch", MRUBY_TAG, MRUBY_REPO, MRUBY_DIR
    end
    actual = IO.popen(["git", "-C", MRUBY_DIR, "rev-parse", "HEAD"], &:read).strip
    unless actual == MRUBY_COMMIT
      abort "#{MRUBY_DIR} が想定したコミットではありません。\n  想定: #{MRUBY_COMMIT}\n  実際: #{actual}"
    end
  end

  desc "libmruby.a をビルドする"
  task build: :fetch do
    enable_toolchain
    env = {
      "MRUBY_CONFIG" => File.expand_path(MRUBY_CONFIG),
      "MRUBY_BUILD_DIR" => File.expand_path(MRUBY_BUILD_DIR)
    }
    sh env, RbConfig.ruby, "-S", "rake", "-m", "-j#{Etc.nprocessors}", chdir: MRUBY_DIR
  end
end

file LIBMRUBY => MRUBY_CONFIG do
  Rake::Task["mruby:build"].invoke
end

HEADERS = FileList["src/*.h"]
OBJS = FileList["src/*.cpp"].pathmap("#{OBJ_DIR}/%n.o")
RES_OBJ = "#{OBJ_DIR}/pikulix.res.o"

# スクリプト API のうち Ruby で書いた部分（src/prelude.rb）を、exe に埋め込むために
# C++ の配列にする。
PRELUDE_INC = "#{GEN_DIR}/prelude.inc"

file PRELUDE_INC => ["src/prelude.rb", __FILE__] do |t|
  mkdir_p GEN_DIR
  rows = File.binread("src/prelude.rb").bytes.each_slice(20).map { |slice| "  #{slice.join(',')}," }
  File.write(t.name, <<~CPP)
    // src/prelude.rb から生成した。編集しないこと。
    static const unsigned char kPreludeSource[] = {
    #{rows.join("\n")}
    };
  CPP
end

FileList["src/*.cpp"].each do |src|
  file src.pathmap("#{OBJ_DIR}/%n.o") => [src, *HEADERS, PRELUDE_INC, LIBMRUBY, __FILE__] do |t|
    mkdir_p OBJ_DIR
    enable_toolchain
    sh "g++", *CXXFLAGS, "-I", MRUBY_INCLUDE_DIR, "-I", GEN_DIR, "-c", src, "-o", t.name
  end
end

# マニフェスト（DPI 対応などの宣言）を exe に埋め込むためのリソース。
file RES_OBJ => ["src/pikulix.rc", "src/pikulix.manifest"] do |t|
  enable_toolchain
  mkdir_p OBJ_DIR
  sh "windres", "-I", "src", "-O", "coff", "-i", "src/pikulix.rc", "-o", t.name
end

file EXE => [*OBJS, RES_OBJ, LIBMRUBY] do |t|
  enable_toolchain
  sh "g++", *LDFLAGS, "-o", t.name, *OBJS, RES_OBJ, "-L", MRUBY_LIB_DIR, *LIBS
end

desc "pikulix.exe の依存 DLL が Windows 標準のものだけであることを確かめる"
task check_deps: EXE do
  enable_toolchain
  dump = IO.popen(["objdump", "-p", EXE], &:read)
  dlls = dump.scan(/^\s*DLL Name: (\S+)/).flatten.uniq.sort_by(&:downcase)
  abort "依存 DLL を読み取れませんでした" if dlls.empty?

  unknown = dlls.reject { |dll| ALLOWED_DLLS.any? { |pattern| pattern.match?(dll) } }
  puts "依存 DLL:"
  dlls.each { |dll| puts "  #{dll}#{'  <-- 想定外' if unknown.include?(dll)}" }
  abort "想定外の DLL に依存しています: #{unknown.join(', ')}" unless unknown.empty?
end

desc "pikulix.exe をビルドする"
task build: :check_deps do
  puts "#{EXE} (#{File.size(EXE) / 1024} KB)"
end

desc "画面を使わないテストを実行する"
task test: :build do
  ruby "test/run.rb"
end

desc "画像を探す処理の所要時間をはかる（画面は使わない）"
task bench: :build do
  ruby "test/bench.rb"
end

desc "Windows サンドボックスで確かめるためのファイルを #{SANDBOX_DIR} に用意する"
task sandbox: :build do
  share = "#{SANDBOX_DIR}/pikulix"
  rm_rf SANDBOX_DIR
  mkdir_p share
  cp EXE, share
  cp "samples/hello.rb", share
  cp "samples/hello.rb", "#{share}/こんにちは.rb" # 日本語のファイル名でも動くことを確かめる

  # .wsb はサンドボックスの設定ファイル。共有するフォルダを絶対パスで書く必要がある。
  host_folder = File.expand_path(share).tr("/", "\\")
  sandbox_folder = "C:\\Users\\WDAGUtilityAccount\\Desktop\\pikulix"
  wsb = "#{SANDBOX_DIR}/pikulix.wsb"
  File.write(wsb, <<~XML)
    <Configuration>
      <Networking>Disable</Networking>
      <MappedFolders>
        <MappedFolder>
          <HostFolder>#{host_folder}</HostFolder>
          <SandboxFolder>#{sandbox_folder}</SandboxFolder>
          <ReadOnly>true</ReadOnly>
        </MappedFolder>
      </MappedFolders>
      <LogonCommand>
        <Command>explorer.exe #{sandbox_folder}</Command>
      </LogonCommand>
    </Configuration>
  XML

  puts
  puts "#{wsb} を開くと、サンドボックスが起動してフォルダが開きます。"
  puts "その中で、hello.rb や こんにちは.rb を pikulix.exe にドラッグ＆ドロップしてください。"
end

task default: :build
