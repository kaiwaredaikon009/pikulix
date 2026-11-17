# frozen_string_literal: true

# スクリプト API（src/prelude.rb と src/api.cpp）のテスト。
#
# テストモード（--screen-image）で実行する。画面の代わりに画像ファイルを探し、
# マウスやキーボードの操作は実際には送られない。何をしようとしたかは、ログで確かめる。

require_relative "support/helper"

class ApiTest < Minitest::Test
  include PikulixHelper

  def setup
    save_image("button", region(:button_enabled))
    save_image("icon", region(:icon))
    save_image("absent", Canvas.new(64, 48).noise(0, 0, 64, 48, Random.new(3)))
  end

  # ログのうち、実行のたびに変わる部分（かかった時間）を固定の文字にする。
  def run_script(source)
    out, err, status = run_on_image(screen_path, source)
    [out.gsub(/\d+ ms/, "N ms"), err, status]
  end

  def test_findは見つけた場所を返す
    out, err, status = run_script(<<~RUBY)
      m = find "button.png"
      p m
      p [m.x, m.y, m.w, m.h, m.score, m.center]
    RUBY
    assert_equal <<~OUT, out
      [pikulix] find "button.png": (300, 620) で発見（一致度 1.000、N ms）
      #<Pikulix::Match (300, 620) 120x32 一致度 1.000>
      [300, 620, 120, 32, 1.0, [360, 636]]
    OUT
    assert_equal "", err
    assert_equal 0, status
  end

  def test_マウスの操作は対象の中心に行う
    out, err, status = run_script(<<~RUBY)
      click "button.png"
      double_click "button.png", offset: [10, -5]
      right_click [100, 200]
      hover find("icon.png")
      click
      drag_drop "button.png", [800, 400]
      wheel :down, 3
      wheel "icon.png", :up
    RUBY
    assert_equal <<~OUT, out
      [pikulix] click "button.png": (300, 620) で発見（一致度 1.000、N ms）
      [pikulix] click (360, 636)
      [pikulix] double_click "button.png": (300, 620) で発見（一致度 1.000、N ms）
      [pikulix] double_click (370, 631)
      [pikulix] right_click (100, 200)
      [pikulix] find "icon.png": (1200, 500) で発見（一致度 1.000、N ms）
      [pikulix] hover (1212, 512)
      [pikulix] click (1212, 512)
      [pikulix] drag_drop "button.png": (300, 620) で発見（一致度 1.000、N ms）
      [pikulix] drag_drop (360, 636) → (800, 400)
      [pikulix] wheel down 3
      [pikulix] wheel "icon.png": (1200, 500) で発見（一致度 1.000、N ms）
      [pikulix] wheel up 1
    OUT
    assert_equal "", err
    assert_equal 0, status
  end

  def test_キーボードの操作
    out, err, status = run_script(<<~RUBY)
      Pikulix.delay = 0
      type "こんにちは\\n"
      key "ctrl+s"
      key "ctrl+shift+esc", "alt+f4", "F5", "ctrl++", "win"
      paste "貼り付ける文字列"
      run "notepad.exe", "a b.txt"
    RUBY
    assert_equal <<~OUT, out
      [pikulix] type "こんにちは\\n"
      [pikulix] key "ctrl+s"
      [pikulix] key "ctrl+shift+esc"
      [pikulix] key "alt+f4"
      [pikulix] key "F5"
      [pikulix] key "ctrl++"
      [pikulix] key "win"
      [pikulix] paste "貼り付ける文字列"
      [pikulix] run "notepad.exe", "a b.txt"
    OUT
    assert_equal "", err
    assert_equal 0, status
  end

  def test_知らないキーの名前は例外になる
    out, err, status = run_script(%(key "ctrl+nosuchkey"\n))
    assert_equal %([pikulix] key "ctrl+nosuchkey"\n), out
    assert_match(/script\.rb:1: キーの名前が分かりません: nosuchkey \(Pikulix::Error\)/, err)
    assert_equal 1, status
  end

  def test_existsは無ければnilを返す
    out, err, status = run_script(<<~RUBY)
      p exists("absent.png", 0)
      p exists("absent.png", timeout: 0)
      p exists("button.png").class
    RUBY
    assert_equal <<~OUT, out
      [pikulix] exists "absent.png": 見つからない
      nil
      [pikulix] exists "absent.png": 見つからない
      nil
      [pikulix] exists "button.png": (300, 620) で発見（一致度 1.000、N ms）
      Pikulix::Match
    OUT
    assert_equal "", err
    assert_equal 0, status
  end

  def test_wait_vanishは消えたかどうかを返す
    out, err, status = run_script(<<~RUBY)
      p wait_vanish("absent.png", 0)
      p wait_vanish("button.png", 0)
    RUBY
    assert_equal <<~OUT, out
      [pikulix] wait_vanish "absent.png": 消えた
      true
      [pikulix] wait_vanish "button.png": 消えない
      false
    OUT
    assert_equal "", err
    assert_equal 0, status
  end

  def test_見つからなければtimeoutまで待つ
    out, _err, status = run_script(<<~RUBY)
      Pikulix.log = false
      start = Pikulix._clock
      exists("absent.png", 0.5)
      waited = Pikulix._clock - start
      puts waited >= 0.5 && waited < 2.0
    RUBY
    assert_equal "true\n", out
    assert_equal 0, status
  end

  def test_findは見つからなければ例外を送出し画面を保存する
    out, err, status = run_script(<<~RUBY)
      puts "前"
      find "absent.png", timeout: 0
      puts "後"
    RUBY
    lines = out.lines(chomp: true)
    assert_equal "前", lines[0]
    assert_equal %([pikulix] find "absent.png": 見つからない（0.0 秒待機、しきい値 0.7）), lines[1]
    saved = lines[2][/\A\[pikulix\] そのときの画面を保存しました: (.+\\logs\\failed-\d{8}-\d{6}-absent(-\d+)?\.png)\z/, 1]
    assert saved, "保存先の行が無い: #{lines[2].inspect}"
    assert_equal 3, lines.size, "例外のあとは実行されない"

    # 保存されたのが PNG であること。確かめたら消す。
    assert_equal "\x89PNG\r\n\x1a\n".b, File.binread(saved, 8)
    File.delete(saved)

    # エラーの場所は、利用者のスクリプトの行になる。
    assert_match(/script\.rb:2: "absent\.png" が見つかりません（0\.0 秒待機、しきい値 0\.7） \(Pikulix::FindFailed\)/, err)
    refute_match(/\(pikulix\)/, err, "組み込みの行が表示されている")
    assert_equal 1, status
  end

  def test_FindFailedはrescueできる
    out, err, status = run_script(<<~RUBY)
      Pikulix.log = false
      begin
        click "absent.png", timeout: 0
      rescue FindFailed => e
        puts "rescue: \#{e.message}"
      end
      puts FindFailed.ancestors.include?(StandardError)
    RUBY
    assert_equal <<~OUT, out
      rescue: "absent.png" が見つかりません（0.0 秒待機、しきい値 0.7）
      true
    OUT
    assert_equal "", err
    assert_equal 0, status
    Dir.glob(File.join(ROOT, "build", "logs", "failed-*-absent*.png")).each { |path| File.delete(path) }
  end

  def test_しきい値と既定値を変えられる
    rng = Random.new(1)
    save_image("button_noisy", region(:button_enabled).map_values { |v| v + rng.rand(-40..40) })
    out, err, status = run_script(<<~RUBY)
      Pikulix.log = false
      Pikulix.timeout = 0
      p !exists("button_noisy.png").nil?
      p !exists("button_noisy.png", similarity: 0.99).nil?
      Pikulix.similarity = 0.99
      p !exists("button_noisy.png").nil?
    RUBY
    assert_equal "true\nfalse\nfalse\n", out
    assert_equal "", err
    assert_equal 0, status
  end

  def test_画像のファイルが無ければ例外になる
    _out, err, status = run_script(%(find "存在しない.png"\n))
    assert_match(/script\.rb:1: 画像を読めません: .*存在しない\.png（ファイルがありません） \(Pikulix::Error\)/, err)
    assert_equal 1, status
  end

  def test_引数が正しくなければ例外になる
    {
      %(find 123) => /TypeError/,
      %(click "button.png", similarity: 1.5) => /similarity.*ArgumentError/,
      %(click "button.png", timeout: -1) => /timeout.*ArgumentError/,
      %(click "button.png", offset: 5) => /offset.*ArgumentError/,
      %(click :button) => /TypeError/,
      %(wheel :sideways) => /wheel.*ArgumentError/,
      %(type 123) => /TypeError/,
      %(sleep -1) => /ArgumentError/
    }.each do |source, pattern|
      _out, err, status = run_script("Pikulix.log = false\n#{source}\n")
      assert_match(/script\.rb:2: /, err, source)
      assert_match(pattern, err, source)
      assert_equal 1, status, source
    end
  end

  def test_画像は日本語のファイル名でも読める
    save_image("ボタン", region(:button_enabled))
    out, err, status = run_script(%(m = find "ボタン.png"\nputs m.x\n))
    assert_equal %([pikulix] find "ボタン.png": (300, 620) で発見（一致度 1.000、N ms）\n300\n), out
    assert_equal "", err
    assert_equal 0, status
  end

  def test_waitとsleep
    out, err, status = run_script(<<~RUBY)
      Pikulix.log = false
      p wait("button.png", 1).x
      start = Pikulix._clock
      sleep 0.2
      wait 0.1
      waited = Pikulix._clock - start
      puts waited >= 0.3 && waited < 1.0
    RUBY
    assert_equal "300\ntrue\n", out
    assert_equal "", err
    assert_equal 0, status
  end

  def test_exitは終了コードを返す
    out, err, status = run_script(%(puts "前"\nexit 7\nputs "後"\n))
    assert_equal "前\n", out
    assert_equal "", err
    assert_equal 7, status

    _out, _err, status = run_script(%(exit\n))
    assert_equal 0, status
    _out, _err, status = run_script(%(exit false\n))
    assert_equal 1, status
  end

  def test_テストモードの画像が読めなければ終了コード2を返す
    _out, err, status = run_pikulix("--screen-image", File.join(WORK, "無い.png"), script("utf8.rb"))
    assert_match(/画像を読めません.*無い\.png/, err)
    assert_equal 2, status
  end

  def test_知らないオプションは使い方を出して終了コード2を返す
    _out, err, status = run_pikulix("--unknown", script("utf8.rb"))
    assert_match(/使い方/, err)
    assert_equal 2, status
  end
end
