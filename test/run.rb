# frozen_string_literal: true

# 画面を使わないテスト。build/pikulix.exe を実際に起動して確かめる。
# `rake test` から実行する。
#
#   host_test.rb   スクリプトを読んで実行する部分
#   match_test.rb  画像を探す処理
#   api_test.rb    スクリプト API（テストモードで、操作は実際には送らない）
#   file_test.rb   ファイルの読み書き
#   samples_test.rb  サンプルスクリプト（samples/）

Dir.glob(File.join(__dir__, "*_test.rb")).sort.each { |path| require path }
