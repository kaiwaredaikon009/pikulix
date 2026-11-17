# mruby のビルド設定。Rakefile の mruby:build タスクが MRUBY_CONFIG として渡す。
#
# ここで MRB_ で始まる定義（MRB_NO_BOXING など）を足すと、mruby の構造体の
# 配置が変わる。その場合は、ホスト（src/）のコンパイルにも同じ定義が要る。

MRuby::Build.new do |conf|
  conf.toolchain :gcc

  # バージョンの固定は Rakefile 側で行う（タグとコミットを照合する）ので、
  # mruby のロックファイル（mruby_build_config.rb.lock）は作らせない。
  MRuby::Lockfile.disable
  conf.disable_lock

  # 文字列を UTF-8 として扱う（"こんにちは".size が 15 でなく 5 になる）。
  # Rakefile の MRUBY_DEFINES と揃えること。
  conf.defines << "MRB_UTF8_STRING"

  # 使える機能は、ここに含めた mrbgems で決まる。
  # コマンド（mruby、mirb など）を作る mruby-bin-* は含めない。
  #
  # stdlib-io（mruby-io、mruby-socket など）は含めない。mruby-io は Windows で
  # ネットワーク用の DLL（WS2_32.dll）をリンクするためである。その結果 File や
  # $stdout は使えず、puts は src/main.cpp で定義している。
  conf.gembox "stdlib"
  conf.gembox "stdlib-ext"
  conf.gembox "math"
  conf.gembox "metaprog" # 実行時に .rb を読むための mruby-compiler を含む

  # mruby 自身の Ruby 部分（mrblib）にも行番号を残す。
  # conf.enable_debug は最適化を切り、アサーションも有効にするので使わない。
  conf.mrbc.compile_options += " -g"
end
