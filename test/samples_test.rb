# frozen_string_literal: true

# サンプルスクリプト（samples/）のテスト。配布物に入るので、壊れていないことを確かめる。
#
# テストモード（--screen-image）で実行するので、マウスやキーボードの操作は送られず、
# run もアプリを起動しない。hello.rb は、メッセージボックスが閉じられるまで終わらない
# ので、文法だけを確かめる。
#
# ファイルを読み書きするサンプルは、WORK に写してから実行する（samples/ に
# result.txt などを作らないため）。

require_relative "support/helper"

class SamplesTest < Minitest::Test
  include PikulixHelper

  SAMPLES = File.join(ROOT, "samples")
  DIR = File.join(WORK, "samples")

  def setup
    FileUtils.rm_rf(DIR)
    FileUtils.mkdir_p(DIR)
  end

  # サンプルを WORK に写す。files はサンプルと一緒に写すファイル。
  def copy_sample(name, *files)
    [name, *files].each { |file| FileUtils.cp(File.join(SAMPLES, file), DIR) }
    File.join(DIR, name)
  end

  def test_どのサンプルも文法が正しい
    paths = Dir.glob(File.join(SAMPLES, "*.rb"))
    refute_empty paths
    paths.each do |path|
      RubyVM::InstructionSequence.compile_file(path)
    rescue SyntaxError => e
      flunk "#{File.basename(path)}: #{e.message}"
    end
  end

  def test_notepad
    out, err, status = run_pikulix("--screen-image", screen_path, File.join(SAMPLES, "notepad.rb"))
    assert_equal <<~'OUT', out
      [pikulix] run "notepad.exe"
      [pikulix] type "Pikulix から入力しています。\n"
      [pikulix] type "日本語も、記号 (!\"\#$%&'[]{}@) も、キーボードの配列に関係なく入力できます。\n"
      [pikulix] paste "この行は、クリップボード経由で貼り付けました。"
      [pikulix] key "enter"
      [pikulix] key "ctrl+a"
    OUT
    assert_equal "", err
    assert_equal 0, status
  end

  def test_list_input
    script = copy_sample("list_input.rb", "list.tsv")
    out, err, status = run_pikulix("--screen-image", screen_path, script)
    assert_equal <<~'OUT', out
      [pikulix] run "notepad.exe"
      [pikulix] type "りんご 3 個 × 120 円 = 360 円\n"
      [pikulix] type "みかん 10 個 × 40 円 = 400 円\n"
      [pikulix] type "ぶどう 1 個 × 580 円 = 580 円\n"
      [pikulix] type "合計 1340 円\n"
      result.txt に結果を書き足しました
    OUT
    assert_equal "", err
    assert_equal 0, status
    assert_match(/\A\d{4}-\d\d-\d\d \d\d:\d\d:\d\d 3 件を入力しました（合計 1340 円）\n\z/,
                 File.read(File.join(DIR, "result.txt"), encoding: "UTF-8"))
  end

  def test_click_image
    script = copy_sample("click_image.rb")
    region(:button_enabled).save(File.join(DIR, "target.png"))
    out, err, status = run_pikulix("--screen-image", screen_path, script)
    assert_equal <<~OUT, out.gsub(/\d+ ms/, "N ms")
      [pikulix] exists "target.png": (300, 620) で発見（一致度 1.000、N ms）
      [pikulix] click "target.png": (300, 620) で発見（一致度 1.000、N ms）
      [pikulix] click (360, 636)
    OUT
    assert_equal "", err
    assert_equal 0, status
  end
end
