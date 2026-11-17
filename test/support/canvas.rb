# frozen_string_literal: true

# テスト用の画像を作るための道具。画像は、実際の画面を写したものではなく、
# すべてここで描いて作る（公開リポジトリに個人情報を入れないため）。

require "zlib"

# 画像。各行を、1 画素 3 バイト（R、G、B）のバイナリ文字列で持つ。
class Canvas
  attr_reader :width, :height, :rows

  # 「文字」の代わりに使う、5×7 の模様。乱数で作るが、種を固定しているので毎回同じ。
  GLYPH_W = 5
  GLYPH_H = 7
  GLYPHS = begin
    rng = Random.new(7)
    Array.new(26) { Array.new(GLYPH_H) { Array.new(GLYPH_W) { rng.rand < 0.45 } } }
  end.freeze

  def initialize(width, height, color = [0, 0, 0])
    @width = width
    @height = height
    row = color.pack("C3") * width
    @rows = Array.new(height) { row.dup }
  end

  def fill(x, y, w, h, color)
    part = color.pack("C3") * w
    h.times { |j| @rows[y + j][x * 3, w * 3] = part }
    self
  end

  # 左から右へ色が変わるグラデーション。
  def gradient(x, y, w, h, from, to)
    part = String.new(capacity: w * 3, encoding: Encoding::BINARY)
    w.times do |i|
      t = w > 1 ? i.fdiv(w - 1) : 0.0
      part << from.zip(to).map { |a, b| (a + (b - a) * t).round }.pack("C3")
    end
    h.times { |j| @rows[y + j][x * 3, w * 3] = part }
    self
  end

  # でたらめな色で埋める。
  def noise(x, y, w, h, rng)
    h.times { |j| @rows[y + j][x * 3, w * 3] = rng.bytes(w * 3) }
    self
  end

  def set(x, y, color)
    @rows[y][x * 3, 3] = color.pack("C3")
  end

  def get(x, y)
    @rows[y].byteslice(x * 3, 3).unpack("C3")
  end

  # 「文字列」を描く。indices は GLYPHS の番号の並び。1 文字の幅は 6 画素。
  def glyphs(x, y, indices, color)
    packed = color.pack("C3")
    indices.each_with_index do |index, n|
      GLYPHS[index % GLYPHS.size].each_with_index do |bits, j|
        bits.each_with_index do |on, i|
          @rows[y + j][(x + n * (GLYPH_W + 1) + i) * 3, 3] = packed if on
        end
      end
    end
    self
  end

  def crop(x, y, w, h)
    result = Canvas.new(w, h)
    h.times { |j| result.rows[j] = @rows[y + j].byteslice(x * 3, w * 3) }
    result
  end

  def blit(other, x, y)
    other.height.times { |j| @rows[y + j][x * 3, other.width * 3] = other.rows[j] }
    self
  end

  # 各画素の各色を、ブロックで変換した画像を返す。ブロックは 0〜255 の値を受け取る。
  def map_values
    result = Canvas.new(@width, @height)
    @rows.each_with_index do |row, j|
      result.rows[j] = row.unpack("C*").map { |v| yield(v).clamp(0, 255) }.pack("C*")
    end
    result
  end

  # 各画素を、ブロックで変換した画像を返す。ブロックは [r, g, b] を受け取って返す。
  def map_pixels
    result = Canvas.new(@width, @height)
    @rows.each_with_index do |row, j|
      result.rows[j] = row.unpack("C*").each_slice(3).flat_map { |rgb| yield(rgb) }.pack("C*")
    end
    result
  end

  # 色ごとの値の配列（[r の配列, g の配列, b の配列]）。行の順に並ぶ。
  def channels
    values = @rows.join.unpack("C*")
    Array.new(3) { |c| (0...(@width * @height)).map { |i| values[i * 3 + c] } }
  end

  def save(path)
    raw = @rows.map { |row| "\x00".b + row }.join
    chunk = lambda do |type, data|
      [data.bytesize].pack("N") + type + data + [Zlib.crc32(type + data)].pack("N")
    end
    File.binwrite(path, "\x89PNG\r\n\x1a\n".b +
                        chunk.call("IHDR".b, [@width, @height, 8, 2, 0, 0, 0].pack("NNCCCCC")) +
                        chunk.call("IDAT".b, Zlib::Deflate.deflate(raw, Zlib::BEST_SPEED)) +
                        chunk.call("IEND".b, "".b))
    path
  end
end

# フル HD の画面を模した画像。ウィンドウ、文字、ボタン、アイコンなどを置く。
module TestScreen
  WIDTH = 1920
  HEIGHT = 1080

  BLUE = [0, 120, 215].freeze
  WHITE = [255, 255, 255].freeze
  LABEL = [3, 14, 1, 5, 9, 2, 6, 5, 3].freeze # ボタンに描く「文字列」

  # 画像の中の、名前を付けた場所。[x, y, w, h]
  REGIONS = {
    text: [255, 233, 100, 40],           # 文字の並んだ部分（位置は半端な値にしてある）
    button_enabled: [300, 620, 120, 32],
    button_disabled: [500, 620, 120, 32],
    noise: [1217, 163, 64, 48],          # でたらめな模様
    large: [150, 100, 400, 300],         # ウィンドウの広い範囲
    icon: [1200, 500, 24, 24],           # 同じものが (1400, 700) にもある
    icon_second: [1400, 700, 24, 24],
    red: [1500, 800, 200, 100],          # 単色の領域
    tiny: [613, 1047, 8, 8]              # タスクバーのアイコンの一部
  }.freeze

  module_function

  def build
    rng = Random.new(20_261_116)
    c = Canvas.new(WIDTH, HEIGHT)
    c.gradient(0, 0, WIDTH, HEIGHT - 48, [20, 50, 100], [60, 110, 170])

    # ウィンドウ: 青いタイトルバーと、白い地に「文字」の行
    c.fill(100, 80, 900, 700, WHITE)
    c.fill(100, 80, 900, 32, BLUE)
    c.glyphs(112, 92, Array.new(20) { rng.rand(26) }, WHITE)
    20.times do |line|
      words = Array.new(110) { rng.rand < 0.15 ? nil : rng.rand(26) }
      words.each_with_index do |index, n|
        c.glyphs(130 + n * 6, 140 + line * 22, [index], [40, 40, 40]) if index
      end
    end

    # 有効なボタン（青地に白い文字）と、無効なボタン（灰色の地に、やや濃い灰色の文字）
    button(c, *REGIONS[:button_enabled][0, 2], BLUE, WHITE)
    button(c, *REGIONS[:button_disabled][0, 2], [204, 204, 204], [160, 160, 160])

    c.noise(1100, 100, 400, 300, rng)
    c.fill(*REGIONS[:red], [200, 30, 30])

    # 同じアイコンを 2 か所に置く
    icon = Canvas.new(24, 24)
    6.times { |j| 6.times { |i| icon.fill(i * 4, j * 4, 4, 4, Array.new(3) { rng.rand(256) }) } }
    c.blit(icon, *REGIONS[:icon][0, 2])
    c.blit(icon, *REGIONS[:icon_second][0, 2])

    # タスクバーと、その上のアイコン
    c.fill(0, HEIGHT - 48, WIDTH, 48, [32, 32, 32])
    14.times do |n|
      taskbar_icon = Canvas.new(32, 32)
      8.times { |j| 8.times { |i| taskbar_icon.fill(i * 4, j * 4, 4, 4, Array.new(3) { rng.rand(256) }) } }
      c.blit(taskbar_icon, 600 + n * 48, HEIGHT - 40)
    end
    c
  end

  def button(canvas, x, y, background, foreground)
    canvas.fill(x, y, 120, 32, background)
    canvas.glyphs(x + 33, y + 12, LABEL, foreground)
  end
end
