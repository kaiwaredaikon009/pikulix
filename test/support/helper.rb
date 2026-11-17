# frozen_string_literal: true

# テストに共通の道具。

require "minitest/autorun"
require "open3"
require "fileutils"
require_relative "canvas"

module PikulixHelper
  ROOT = File.expand_path("../..", __dir__)
  EXE = File.join(ROOT, "build", "pikulix.exe")
  SCRIPTS = File.join(ROOT, "test", "scripts")
  WORK = File.join(ROOT, "build", "test") # テストが作る画像とスクリプトの置き場所

  FileUtils.mkdir_p(WORK)

  # 標準出力・標準エラーは UTF-8 のバイト列として受け取り、改行を LF に揃える。
  def run_pikulix(*args)
    out, err, status = Open3.capture3(EXE, *args, binmode: true)
    [out, err].each { |s| s.force_encoding("UTF-8").gsub!("\r\n", "\n") }
    [out, err, status.exitstatus]
  end

  def script(name)
    File.join(SCRIPTS, name)
  end

  # WORK にスクリプトを書き、テストモード（画面の代わりに画像を使う）で実行する。
  # スクリプトの中の画像は、WORK からの相対パスで指定する。
  def run_on_image(screen_png, source, name: "script.rb")
    path = File.join(WORK, name)
    File.write(path, source)
    run_pikulix("--screen-image", screen_png, path)
  end

  def save_image(name, canvas)
    canvas.save(File.join(WORK, "#{name}.png"))
  end

  # フル HD の画面を模した画像。作るのに時間がかかるので、1 回だけ作って使い回す。
  def self.screen
    @screen ||= TestScreen.build.tap { |canvas| canvas.save(screen_path) }
  end

  def self.screen_path
    File.join(WORK, "screen.png")
  end

  def screen
    PikulixHelper.screen
  end

  def screen_path
    PikulixHelper.screen
    PikulixHelper.screen_path
  end

  def region(name)
    screen.crop(*TestScreen::REGIONS.fetch(name))
  end

  # 画像を探す。names は WORK にある画像の名前（拡張子なし）。
  # 名前ごとに { found:, x:, y:, score:, ms: } を返す。
  def search(names, similarity: 0.7, on: screen_path)
    lines = names.map do |name|
      <<~RUBY
        found, x, y, _w, _h, score, ms = Pikulix._find("#{name}.png", #{similarity})
        puts [found, x, y, score.inspect, ms].join(" ")
      RUBY
    end
    out, err, status = run_on_image(on, lines.join)
    assert_equal "", err
    assert_equal 0, status
    results = out.lines.map do |line|
      found, x, y, score, ms = line.split
      { found: found == "true", x: x.to_i, y: y.to_i, score: score == "nil" ? nil : score.to_f, ms: ms.to_f }
    end
    names.zip(results).to_h
  end
end
