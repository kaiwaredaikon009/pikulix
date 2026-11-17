# frozen_string_literal: true

# ファイルの読み書き（read_text、write_text、append_text、file_exist?）のテスト。
#
# 画面は要らないので、ふつうに（テストモードではなく）実行する。
# スクリプトは WORK に置くので、スクリプトの中の相対パスは WORK からの相対になる。

require_relative "support/helper"

class FileIoTest < Minitest::Test
  include PikulixHelper

  DIR = File.join(WORK, "file")

  def setup
    FileUtils.rm_rf(DIR)
    FileUtils.mkdir_p(DIR)
  end

  def run_script(source, name: "file_script.rb")
    path = File.join(WORK, name)
    File.write(path, source)
    run_pikulix(path)
  end

  def test_書いて読む
    out, err, status = run_script(<<~RUBY)
      p file_exist?("file/新規.txt")
      write_text "file/新規.txt", "一行目\n二行目\n"
      p file_exist?("file/新規.txt")
      p read_text("file/新規.txt")
      write_text "file/新規.txt", "上書き"
      p read_text("file/新規.txt")
      append_text "file/新規.txt", "\n追記"
      p read_text("file/新規.txt")
      append_text "file/追記だけ.txt", "最初から追記"
      p read_text("file/追記だけ.txt")
    RUBY
    assert_equal <<~'OUT', out
      false
      true
      "一行目\n二行目\n"
      "上書き"
      "上書き\n追記"
      "最初から追記"
    OUT
    assert_equal "", err
    assert_equal 0, status
    assert_equal "上書き\n追記".b, File.binread(File.join(DIR, "新規.txt"))
  end

  def test_相対パスはスクリプトのあるフォルダが基準になる
    Dir.chdir(DIR) do
      _out, err, status = run_script('write_text "file/相対.txt", "x"')
      assert_equal "", err
      assert_equal 0, status
    end
    assert File.exist?(File.join(DIR, "相対.txt")), "カレントディレクトリではなく、スクリプトのあるフォルダに書く"
  end

  def test_絶対パスも使える
    path = File.join(DIR, "絶対.txt").tr("/", "\\")
    out, err, status = run_script(<<~RUBY)
      write_text #{path.inspect}, "abc"
      print read_text(#{path.inspect})
    RUBY
    assert_equal "abc", out
    assert_equal "", err
    assert_equal 0, status
  end

  def test_BOMとUTF16と改行コード
    File.binwrite(File.join(DIR, "bom.txt"), "\xEF\xBB\xBF名前\tて\r\n")
    File.binwrite(File.join(DIR, "utf16.txt"), "\xFF\xFE" + "名前\tね\r\n".encode("UTF-16LE").b)
    out, err, status = run_script(<<~RUBY)
      p read_text("file/bom.txt")
      p read_text("file/utf16.txt")
      p read_text("file/bom.txt").lines.map { |l| l.chomp.split("\t") }
    RUBY
    assert_equal <<~'OUT', out
      "名前\tて\r\n"
      "名前\tね\r\n"
      [["名前", "て"]]
    OUT
    assert_equal "", err
    assert_equal 0, status
  end

  def test_空のファイル
    File.binwrite(File.join(DIR, "empty.txt"), "")
    out, err, status = run_script('p read_text("file/empty.txt")')
    assert_equal "\"\"\n", out
    assert_equal "", err
    assert_equal 0, status
  end

  def test_ShiftJISのファイルはエラーになる
    File.binwrite(File.join(DIR, "sjis.txt"), "名前".encode("Shift_JIS").b)
    _out, err, status = run_script('read_text "file/sjis.txt"')
    assert_match(/file_script\.rb:1/, err)
    assert_match(/文字コードが UTF-8 ではありません.*sjis\.txt.*Pikulix::Error/, err)
    assert_equal 1, status
  end

  def test_無いファイルを読むとエラーになる
    _out, err, status = run_script('read_text "file/無い.txt"')
    assert_match(/file_script\.rb:1/, err)
    assert_match(/ファイルを読めません: .*無い\.txt.*Pikulix::Error/, err)
    assert_equal 1, status
  end

  def test_無いフォルダに書くとエラーになる
    _out, err, status = run_script('write_text "file/無いフォルダ/a.txt", "x"')
    assert_match(/ファイルに書けません: .*a\.txt.*Pikulix::Error/, err)
    assert_equal 1, status
  end

  def test_フォルダはfile_existで偽になる
    out, err, status = run_script('p file_exist?("file")')
    assert_equal "false\n", out
    assert_equal "", err
    assert_equal 0, status
  end

  def test_引数の型が違うと例外になる
    _out, err, status = run_script('write_text "file/a.txt", 123')
    assert_match(/書く内容は文字列で指定してください: 123 \(TypeError\)/, err)
    assert_equal 1, status

    _out, err, status = run_script("read_text nil")
    assert_match(/ファイル名は文字列で指定してください: nil \(TypeError\)/, err)
    assert_equal 1, status
  end
end
