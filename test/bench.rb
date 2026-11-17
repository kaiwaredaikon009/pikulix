# frozen_string_literal: true

# 画像を探す処理の所要時間をはかる。`rake bench` から実行する。
#
# フル HD の画面を模した画像の中から、いろいろな画像を探し、1 回の探索にかかる
# 時間を表にする。画面は使わない（キャプチャの時間は含まない）。

require "open3"
require "fileutils"
require_relative "support/canvas"

ROOT = File.expand_path("..", __dir__)
EXE = File.join(ROOT, "build", "pikulix.exe")
WORK = File.join(ROOT, "build", "bench")
RUNS = 7

FileUtils.mkdir_p(WORK)
screen = TestScreen.build
screen.save(File.join(WORK, "screen.png"))

rng = Random.new(5)
absent_text = Canvas.new(100, 40, [255, 255, 255])
3.times { |line| absent_text.glyphs(2, 3 + line * 12, Array.new(16) { rng.rand(26) }, [40, 40, 40]) }
green_button = Canvas.new(120, 32)
TestScreen.button(green_button, 0, 0, [0, 160, 60], [255, 255, 255])

crop = ->(name) { screen.crop(*TestScreen::REGIONS.fetch(name)) }
cases = {
  "文字（100x40）" => crop.call(:text),
  "ボタン（120x32）" => crop.call(:button_enabled),
  "小さなアイコン（24x24）" => crop.call(:icon),
  "ごく小さい（8x8）" => crop.call(:tiny),
  "でたらめな模様（64x48）" => crop.call(:noise),
  "広い範囲（400x300）" => crop.call(:large),
  "とても広い範囲（900x700）" => screen.crop(100, 80, 900, 700),
  "文字にノイズ（100x40）" => crop.call(:text).map_values { |v| v + rng.rand(-40..40) },
  "単色の赤（40x30）" => Canvas.new(40, 30, [200, 30, 30]),
  "単色の白（40x30）" => Canvas.new(40, 30, [255, 255, 255]),
  "無い文字（100x40）" => absent_text,
  "無い模様（64x48）" => Canvas.new(64, 48).noise(0, 0, 64, 48, rng),
  "色の違うボタン（120x32）" => green_button
}

source = +"Pikulix.log = false\n"
cases.each_with_index do |(_label, canvas), index|
  canvas.save(File.join(WORK, "case#{index}.png"))
  source << <<~RUBY
    times = []
    result = nil
    #{RUNS}.times do
      result = Pikulix._find("case#{index}.png", 0.7)
      times << result[6]
    end
    puts [result[0], result[1], result[2], result[5].inspect, times.min, times.sort[times.size / 2]].join(" ")
  RUBY
end
script = File.join(WORK, "bench.rb")
File.write(script, source)

out, err, status = Open3.capture3(EXE, "--screen-image", File.join(WORK, "screen.png"), script)
abort "失敗しました:\n#{err}" unless status.success?

puts "#{TestScreen::WIDTH}x#{TestScreen::HEIGHT} の画像から探す。時間は #{RUNS} 回のうちの最小と中央（ミリ秒）。"
puts
puts format("%-8s %-14s %-8s %7s %7s  %s", "結果", "場所", "一致度", "最小", "中央", "探す画像")
cases.keys.zip(out.lines) do |label, line|
  found, x, y, score, min, median = line.split
  puts format("%-8s %-14s %-8s %7.1f %7.1f  %s",
              found == "true" ? "発見" : "なし",
              found == "true" ? "(#{x}, #{y})" : "-",
              score == "nil" ? "-" : format("%.3f", score.to_f),
              min.to_f, median.to_f, label)
end
