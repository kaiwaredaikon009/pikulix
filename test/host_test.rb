# frozen_string_literal: true

# ホスト（スクリプトを読んで実行する部分）のテスト。

require_relative "support/helper"

class HostTest < Minitest::Test
  include PikulixHelper

  def test_日本語の名前のスクリプトを実行して日本語を出力できる
    out, err, status = run_pikulix(script("出力.rb"))
    assert_equal <<~OUT, out
      こんにちは、世界
      3
      配列は
      1 要素ずつ


      改行つき
      print は改行しない
      "日本語"
      :sym
    OUT
    assert_equal "", err
    assert_equal 0, status
  end

  def test_文字列をUTF8として扱う
    out, err, status = run_pikulix(script("utf8.rb"))
    assert_equal "5\nこ\nはちにんこ\n", out
    assert_equal "", err
    assert_equal 0, status
  end

  def test_BOM付きのスクリプトを実行できる
    out, _err, status = run_pikulix(script("bom.rb"))
    assert_equal "BOM 付き\n", out
    assert_equal 0, status
  end

  def test_例外はファイル名と行番号つきで報告し終了コード1を返す
    out, err, status = run_pikulix(script("エラー.rb"))
    assert_equal "この行は実行される\n", out
    assert_match(/エラー\.rb:5/, err)
    assert_match(/わざと失敗させる \(RuntimeError\)/, err)
    assert_equal 1, status
  end

  def test_文法エラーは行番号つきで報告し終了コード1を返す
    out, err, status = run_pikulix(script("syntax_error.rb"))
    assert_equal "", out
    assert_match(/syntax_error\.rb:3:\d+: syntax error/, err)
    assert_equal 1, err.lines.size, "エラーは 1 行だけ出る"
    assert_equal 1, status
  end

  def test_引数の型が違うと例外になる
    _out, err, status = run_pikulix(script("bad_argument.rb"))
    assert_match(/bad_argument\.rb:2/, err)
    assert_match(/TypeError/, err)
    assert_equal 1, status
  end

  def test_スクリプトが無ければ終了コード2を返す
    _out, err, status = run_pikulix(script("存在しない.rb"))
    assert_match(/スクリプトを読めません.*存在しない\.rb/, err)
    assert_equal 2, status
  end

  # Windows サンドボックスの代わりの確認。PATH を System32 だけにすると、
  # MSYS2 の DLL（libstdc++-6.dll など）が見つからなくなる。
  # それらに依存していれば、exe は起動できずに失敗する。
  def test_PATHを最小にしても起動できる
    env = { "PATH" => File.join(ENV.fetch("SystemRoot"), "System32") }
    out, err, status = Open3.capture3(env, EXE, script("utf8.rb"), binmode: true)
    assert_equal "5\nこ\nはちにんこ\n", out.force_encoding("UTF-8").gsub("\r\n", "\n")
    assert_equal "", err
    assert_equal 0, status.exitstatus
  end

  # 画像の読み込みは、実行時に Windows の部品（WIC）を呼び出す。
  # PATH を最小にした状態でも、それが働くことを確かめる。
  def test_PATHを最小にしても画像を探せる
    save_image("button", region(:button_enabled))
    path = File.join(WORK, "minimal_path.rb")
    File.write(path, %(Pikulix.log = false\nputs find("button.png").x\n))
    env = { "PATH" => File.join(ENV.fetch("SystemRoot"), "System32") }
    out, err, status = Open3.capture3(env, EXE, "--screen-image", screen_path, path, binmode: true)
    assert_equal "300\n", out.gsub("\r\n", "\n")
    assert_equal "", err
    assert_equal 0, status.exitstatus
  end

  def test_引数が無ければ使い方を出して終了コード2を返す
    _out, err, status = run_pikulix
    assert_match(/使い方/, err)
    assert_equal 2, status
  end
end
