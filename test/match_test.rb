# frozen_string_literal: true

# 画像を探す処理（src/match.cpp）のテスト。画面は使わず、画像ファイルの中から探す。

require_relative "support/helper"

class MatchTest < Minitest::Test
  include PikulixHelper

  def test_切り出した画像を元の場所で見つける
    names = %i[text button_enabled button_disabled noise large tiny]
    names.each { |name| save_image(name, region(name)) }
    results = search(names)
    names.each do |name|
      x, y, = TestScreen::REGIONS[name]
      result = results[name]
      assert result[:found], "#{name} が見つからない"
      assert_equal [x, y], [result[:x], result[:y]], "#{name} の場所"
      assert_equal 1.0, result[:score], "#{name} の一致度"
    end
  end

  # 縮小した画像どうしを比べる方式だと、縮小の格子と位置がずれたときに見落としやすい。
  def test_どんな位置にあっても見つける
    spots = Array.new(12) { |k| [130 + k * 37, 136 + k * 13] }
    names = spots.each_with_index.map do |(x, y), k|
      save_image("spot#{k}", screen.crop(x, y, 100, 40))
      "spot#{k}"
    end
    results = search(names)
    names.zip(spots) do |name, (x, y)|
      assert results[name][:found], "#{name} が見つからない"
      assert_equal [x, y], [results[name][:x], results[name][:y]]
    end
  end

  def test_同じものが複数あれば上にあるものを返す
    save_image("icon", region(:icon))
    result = search(["icon"])["icon"]
    assert result[:found]
    assert_equal TestScreen::REGIONS[:icon][0, 2], [result[:x], result[:y]]
  end

  def test_少し違っていても見つける
    rng = Random.new(1)
    save_image("text_noisy", region(:text).map_values { |v| v + rng.rand(-40..40) })
    save_image("large_noisy", region(:large).map_values { |v| v + rng.rand(-40..40) })
    save_image("button_brighter", region(:button_enabled).map_values { |v| v + 15 })
    results = search(%w[text_noisy large_noisy button_brighter])
    { "text_noisy" => :text, "large_noisy" => :large, "button_brighter" => :button_enabled }.each do |name, source|
      result = results[name]
      assert result[:found], "#{name} が見つからない（一致度 #{result[:score].inspect}）"
      assert_equal TestScreen::REGIONS[source][0, 2], [result[:x], result[:y]]
      assert_operator result[:score], :<, 1.0
    end
  end

  def test_形が同じでも色が違えば見つけない
    # 青いボタンを緑にしたもの。画面には、同じ形の青いボタンがある。
    green = Canvas.new(120, 32)
    TestScreen.button(green, 0, 0, [0, 160, 60], [255, 255, 255])
    save_image("button_green", green)
    result = search(["button_green"])["button_green"]
    refute result[:found], "緑のボタンが見つかった（一致度 #{result[:score].inspect}）"
  end

  def test_形が同じでも濃さが違えば見つけない
    # 無効なボタン（薄い文字）の、文字だけを濃くしたもの。
    dark = Canvas.new(120, 32)
    TestScreen.button(dark, 0, 0, [204, 204, 204], [60, 60, 60])
    save_image("button_dark_text", dark)
    result = search(["button_dark_text"])["button_dark_text"]
    refute result[:found], "文字の濃いボタンが見つかった（一致度 #{result[:score].inspect}）"
  end

  def test_しきい値を上げると似ているだけのものは見つけない
    rng = Random.new(2)
    save_image("text_noisy2", region(:text).map_values { |v| v + rng.rand(-40..40) })
    loose = search(["text_noisy2"], similarity: 0.7)["text_noisy2"]
    strict = search(["text_noisy2"], similarity: 0.99)["text_noisy2"]
    assert loose[:found]
    refute strict[:found]
  end

  def test_単色の画像は同じ色の場所で見つける
    save_image("red", Canvas.new(40, 30, [200, 30, 30]))
    save_image("white", Canvas.new(40, 30, [255, 255, 255]))
    save_image("green", Canvas.new(40, 30, [10, 200, 10]))
    results = search(%w[red white green])

    assert results["red"][:found]
    assert_equal TestScreen::REGIONS[:red][0, 2], [results["red"][:x], results["red"][:y]]
    assert_equal 1.0, results["red"][:score]

    white = results["white"]
    assert white[:found]
    found = screen.crop(white[:x], white[:y], 40, 30)
    assert(found.rows.all? { |row| row == "\xFF".b * 120 }, "見つけた場所が白一色ではない")

    refute results["green"][:found], "画面に無い色が見つかった"
  end

  def test_画面に無い画像は見つけない
    rng = Random.new(3)
    save_image("absent_noise", Canvas.new(64, 48).noise(0, 0, 64, 48, rng))
    absent_text = Canvas.new(100, 40, [255, 255, 255])
    3.times { |line| absent_text.glyphs(2, 3 + line * 12, Array.new(16) { rng.rand(26) }, [40, 40, 40]) }
    save_image("absent_text", absent_text)
    results = search(%w[absent_noise absent_text])
    refute results["absent_noise"][:found]
    refute results["absent_text"][:found], "無い文字列が見つかった（一致度 #{results['absent_text'][:score].inspect}）"
  end

  def test_画面より大きい画像は見つけない
    save_image("too_wide", Canvas.new(2000, 10, [1, 2, 3]))
    result = search(["too_wide"])["too_wide"]
    refute result[:found]
    assert_nil result[:score]
  end

  # 小さな画像で、すべての場所の一致度を Ruby で計算し、exe の結果と突き合わせる。
  # 速くする工夫（粗い探索での絞り込み）が、いちばん良い場所を落としていないことを確かめる。
  def test_総当たりの結果と一致する
    rng = Random.new(4)
    small = Canvas.new(64, 48)
    12.times { |j| 16.times { |i| small.fill(i * 4, j * 4, 4, 4, Array.new(3) { rng.rand(256) }) } }
    small = small.map_values { |v| v + rng.rand(-8..8) }
    small.fill(40, 30, 20, 14, [90, 140, 200])
    small_path = File.join(WORK, "small.png")
    small.save(small_path)

    crop = small.crop(13, 9, 10, 8)
    mean = crop.channels.flatten.sum / (10 * 8 * 3)
    templates = {
      "bf_exact" => crop,
      "bf_noisy" => crop.map_values { |v| v + rng.rand(-30..30) },
      "bf_brighter" => crop.map_values { |v| v + 20 },
      "bf_low_contrast" => crop.map_values { |v| mean + (v - mean) / 2 },
      "bf_absent" => Canvas.new(10, 8).noise(0, 0, 10, 8, rng),
      "bf_flat" => Canvas.new(10, 8, [90, 140, 200]),
      "bf_near_flat" => Canvas.new(10, 8, [90, 140, 200]).tap { |c| c.set(3, 3, [120, 140, 200]) },
      "bf_wide" => small.crop(5, 20, 31, 3),
      "bf_one_pixel" => small.crop(50, 5, 1, 1)
    }
    templates.each { |name, canvas| save_image(name, canvas) }
    results = search(templates.keys, similarity: 0.5, on: small_path)

    hay = small.channels
    templates.each do |name, canvas|
      best_score, best_x, best_y = reference_best(hay, small.width, small.height, canvas)
      result = results[name]
      assert_equal best_score >= 0.5, result[:found], "#{name}: 見つかるかどうか（総当たりの一致度 #{best_score}）"
      next unless result[:found]

      assert_equal [best_x, best_y], [result[:x], result[:y]], "#{name}: 場所"
      assert_in_delta best_score, result[:score], 1e-9, "#{name}: 一致度"
    end
  end

  private

  # すべての場所で一致度を計算し、最大のもの（同じなら上、左が先）を返す。
  def reference_best(hay, hay_w, hay_h, needle)
    needle_channels = needle.channels
    best = [-Float::INFINITY, 0, 0]
    (0..(hay_h - needle.height)).each do |y|
      (0..(hay_w - needle.width)).each do |x|
        score = reference_score(hay, hay_w, needle_channels, needle.width, needle.height, x, y)
        best = [score, x, y] if score > best[0]
      end
    end
    best
  end

  # src/match.cpp の冒頭に書いた一致度の式を、そのまま計算する。
  def reference_score(hay, hay_w, needle, w, h, x0, y0)
    n = w * h
    var_t = var_c = cov = 0
    d2 = 0.0
    3.times do |c|
      st = sc = stt = scc = stc = 0
      h.times do |j|
        base = (y0 + j) * hay_w + x0
        w.times do |i|
          a = hay[c][base + i]
          t = needle[c][j * w + i]
          sc += a
          scc += a * a
          st += t
          stt += t * t
          stc += a * t
        end
      end
      var_t += n * stt - st * st
      var_c += n * scc - sc * sc
      cov += n * stc - st * sc
      d2 += ((st - sc).fdiv(n))**2
    end
    floor = 3.0 * n * n * 4.0
    shape = (2.0 * cov + floor) / (var_t + var_c + floor)
    color = [0.0, 1.0 - (d2 / 3.0) / (64.0 * 64.0)].max
    shape * color
  end
end
