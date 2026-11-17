# Pikulix のスクリプト API。
#
# このファイルは exe に埋め込まれ、利用者のスクリプトより先に読み込まれる。
# C++ 側（src/api.cpp）が用意する Pikulix._find などの単純な関数を組み合わせて、
# find や click といった、スクリプトから使うメソッドをここで定義する。
#
# mruby で動くので、CRuby の標準ライブラリは使えない。

# puts: mruby 4.0 では puts が mruby-io（File や $stdout を提供する mrbgem）に
# 含まれる。mruby-io は Windows でネットワーク用の DLL（WS2_32.dll）を要するので
# 採用せず、コアにある print を使ってここで定義する。
module Kernel
  private def puts(*args)
    if args.empty?
      print "\n"
      return nil
    end
    args.each do |arg|
      if arg.is_a?(Array)
        puts(*arg)
      else
        s = arg.to_s
        print s
        print "\n" unless s[-1] == "\n"
      end
    end
    nil
  end
end

# exit で送出される例外。ホスト（src/main.cpp）が受け取り、終了コードにする。
class SystemExit < Exception
  attr_reader :status

  def initialize(status = 0)
    @status = status
    super("exit")
  end
end

module Pikulix
  class Error < StandardError; end

  # 画像が見つからなかった。
  class FindFailed < Error; end

  # 画像が見つかった場所。
  class Match
    attr_reader :x, :y, :w, :h, :score

    def initialize(x, y, w, h, score)
      @x = x
      @y = y
      @w = w
      @h = h
      @score = score
    end

    # 中心の座標。click などは、ここを対象にする。
    def center
      [@x + @w / 2, @y + @h / 2]
    end

    def to_s
      "(#{@x}, #{@y}) #{@w}x#{@h} 一致度 #{Pikulix._f3(@score)}"
    end

    def inspect
      "#<Pikulix::Match #{self}>"
    end
  end

  SCAN_INTERVAL = 0.1 # 見つからなかったとき、次に探すまでの間隔（秒）
  LEFT = 0
  RIGHT = 1

  @similarity = 0.7 # 一致度のしきい値
  @timeout = 3.0    # 画像が現れるのを待つ秒数
  @delay = 0.2      # 操作のあとに入れる待ち時間（秒）
  @log = true       # 何をしたかを標準出力に出すか

  class << self
    attr_accessor :similarity, :timeout, :delay, :log

    def _log(text)
      print "[pikulix] #{text}\n" if @log
    end

    def _f3(value)
      format("%.3f", value)
    end

    def _similarity_for(value)
      value = @similarity if value.nil?
      unless value.is_a?(Numeric) && value > 0 && value <= 1
        raise ArgumentError, "similarity は 0 より大きく 1 以下の数で指定してください: #{value.inspect}"
      end
      value.to_f
    end

    def _timeout_for(value)
      value = @timeout if value.nil?
      unless value.is_a?(Numeric) && value >= 0
        raise ArgumentError, "timeout は 0 以上の数で指定してください: #{value.inspect}"
      end
      value.to_f
    end

    def _check_path(name, path)
      return if path.is_a?(String) && !path.empty?

      raise TypeError, "#{name} のファイル名は文字列で指定してください: #{path.inspect}"
    end

    def _check_text(name, text)
      return if text.is_a?(String)

      raise TypeError, "#{name} に書く内容は文字列で指定してください: #{text.inspect}"
    end

    def _check_image(image)
      return if image.is_a?(String)

      raise TypeError, "画像はファイル名（文字列）で指定してください: #{image.inspect}"
    end

    # 画像を、見つかるまで（最大 timeout 秒）繰り返し探す。
    # [Match または nil, 最も近い候補の一致度または nil, 探索にかかったミリ秒] を返す。
    def _search(image, similarity, timeout)
      _check_image(image)
      start = _clock
      loop do
        found, x, y, w, h, score, ms = _find(image, similarity)
        return [Match.new(x, y, w, h, score), score, ms] if found
        return [nil, score, ms] if _clock - start >= timeout

        _sleep(SCAN_INTERVAL)
      end
    end

    def _log_found(name, image, match, ms)
      _log "#{name} #{image.inspect}: (#{match.x}, #{match.y}) で発見（一致度 #{_f3(match.score)}、#{ms.round} ms）"
    end

    # 画像を探し、見つからなければ FindFailed を送出する。
    def _find_or_fail(name, image, similarity, timeout)
      similarity = _similarity_for(similarity)
      timeout = _timeout_for(timeout)
      match, score, ms = _search(image, similarity, timeout)
      if match
        _log_found(name, image, match, ms)
        return match
      end

      detail = "#{timeout} 秒待機、しきい値 #{similarity}"
      detail += "、最も近い候補の一致度 #{_f3(score)}" if score
      _log "#{name} #{image.inspect}: 見つからない（#{detail}）"
      saved = _save_screen(image)
      _log "そのときの画面を保存しました: #{saved}" if saved
      raise FindFailed, "#{image.inspect} が見つかりません（#{detail}）"
    end

    # エラーの呼び出し履歴から、このファイル（(pikulix)）と mruby 自身の行を除く。
    # ホスト（src/main.cpp）が、エラーを表示する前に呼ぶ。こうしないと、エラーの
    # 場所として、利用者のスクリプトの行ではなく、このファイルの行が表示される。
    def _clean_backtrace(error)
      trace = error.backtrace
      return if trace.nil?

      cleaned = trace.reject do |line|
        line.start_with?("(pikulix)") || line.include?("/mrblib/") || line.include?("/mrbgems/")
      end
      error.set_backtrace(cleaned) unless cleaned.empty?
    end

    def _exists(image, similarity, timeout)
      similarity = _similarity_for(similarity)
      timeout = _timeout_for(timeout)
      match, _score, ms = _search(image, similarity, timeout)
      if match
        _log_found("exists", image, match, ms)
      else
        _log "exists #{image.inspect}: 見つからない"
      end
      match
    end

    def _wait_vanish(image, similarity, timeout)
      similarity = _similarity_for(similarity)
      timeout = _timeout_for(timeout)
      _check_image(image)
      start = _clock
      vanished = loop do
        found, = _find(image, similarity)
        break true unless found
        break false if _clock - start >= timeout

        _sleep(SCAN_INTERVAL)
      end
      _log "wait_vanish #{image.inspect}: #{vanished ? '消えた' : '消えない'}"
      vanished
    end

    # 操作の対象を、画面上の座標 [x, y] にする。
    #   nil     いまのマウスカーソルの位置
    #   文字列  その画像を探し、見つけた場所の中心
    #   Match   その中心
    #   [x, y]  その座標
    def _point(name, target, similarity, timeout, offset)
      point =
        if target.nil?
          _mouse_pos
        elsif target.is_a?(String)
          _find_or_fail(name, target, similarity, timeout).center
        elsif target.is_a?(Match)
          target.center
        elsif _point?(target)
          [target[0], target[1]]
        else
          raise TypeError, "対象は、画像のファイル名、Match、[x, y] のいずれかで指定してください: #{target.inspect}"
        end
      return point if offset.nil?

      unless _point?(offset)
        raise ArgumentError, "offset は [x, y] の形で指定してください: #{offset.inspect}"
      end
      [point[0] + offset[0], point[1] + offset[1]]
    end

    def _point?(value)
      value.is_a?(Array) && value.size == 2 && value[0].is_a?(Integer) && value[1].is_a?(Integer)
    end

    def _press(button)
      _mouse_button(button, true)
      _sleep(0.03)
      _mouse_button(button, false)
    end

    def _click(name, target, similarity, timeout, offset, button, count)
      x, y = _point(name, target, similarity, timeout, offset)
      _log "#{name} (#{x}, #{y})"
      _mouse_move(x, y)
      _sleep(0.05)
      count.times do |i|
        _sleep(0.08) if i > 0
        _press(button)
      end
      _sleep(@delay)
      nil
    end

    def _drag_drop(from, to, similarity, timeout)
      # 2 つの場所は、ボタンを押す前に決めておく。押したあとで画像が見つからずに
      # 失敗すると、ボタンを押したままになってしまう。
      x1, y1 = _point("drag_drop", from, similarity, timeout, nil)
      x2, y2 = _point("drag_drop", to, similarity, timeout, nil)
      _log "drag_drop (#{x1}, #{y1}) → (#{x2}, #{y2})"
      _mouse_move(x1, y1)
      _sleep(0.1)
      _mouse_button(LEFT, true)
      _sleep(0.2)
      # 一気に動かすと、ドラッグと認識しないアプリがあるので、少しずつ動かす。
      steps = 20
      1.upto(steps) do |i|
        _mouse_move(x1 + (x2 - x1) * i / steps, y1 + (y2 - y1) * i / steps)
        _sleep(0.015)
      end
      _sleep(0.2)
      _mouse_button(LEFT, false)
      _sleep(@delay)
      nil
    end

    # wheel :down, 3 / wheel "list.png", :down, 3
    def _wheel(args, similarity, timeout)
      args = args.dup
      target = nil
      first = args[0]
      target = args.shift if first.is_a?(String) && first != "up" && first != "down"
      target = args.shift if first.is_a?(Match) || first.is_a?(Array)
      direction = args.shift
      steps = args.empty? ? 1 : args.shift
      direction = direction.to_sym if direction.is_a?(String)
      unless args.empty? && (direction == :up || direction == :down) && steps.is_a?(Integer) && steps >= 0
        raise ArgumentError, "wheel は、wheel :down, 3 や wheel \"画像.png\", :up, 2 のように指定してください"
      end

      unless target.nil?
        x, y = _point("wheel", target, similarity, timeout, nil)
        _mouse_move(x, y)
        _sleep(0.05)
      end
      _log "wheel #{direction} #{steps}"
      steps.times do
        _mouse_wheel(direction == :up ? 1 : -1)
        _sleep(0.02)
      end
      _sleep(@delay)
      nil
    end
  end
end

FindFailed = Pikulix::FindFailed

# スクリプトから、レシーバなしで呼ぶメソッド。
module Kernel
  # --- 探す ---

  # 画像を探して、見つけた場所（Pikulix::Match）を返す。
  # 見つからなければ、timeout 秒まで待ってから FindFailed を送出する。
  private def find(image, similarity: nil, timeout: nil)
    Pikulix._find_or_fail("find", image, similarity, timeout)
  end

  # 画像が現れるまで待つ。find と同じだが、待つ秒数を 2 番目の引数でも指定できる。
  # 数だけを渡すと、その秒数だけ待つ（sleep と同じ）。
  private def wait(image, seconds = nil, similarity: nil, timeout: nil)
    return sleep(image) if image.is_a?(Numeric)

    Pikulix._find_or_fail("wait", image, similarity, timeout || seconds)
  end

  # 画像があれば、その場所（Pikulix::Match）を返す。なければ nil を返す。
  # 現れるのを timeout 秒まで待つ。すぐに結果がほしいときは timeout: 0 とする。
  private def exists(image, seconds = nil, similarity: nil, timeout: nil)
    Pikulix._exists(image, similarity, timeout || seconds)
  end

  # 画像が消えるまで待つ。消えたら true、timeout 秒たっても消えなければ false を返す。
  private def wait_vanish(image, seconds = nil, similarity: nil, timeout: nil)
    Pikulix._wait_vanish(image, similarity, timeout || seconds)
  end

  # --- マウス ---
  #
  # 対象（target）には、画像のファイル名、find などが返した Match、座標 [x, y] の
  # いずれかを指定する。省略すると、いまのマウスカーソルの位置になる。
  # offset: [x, y] で、対象からずらした場所を指定できる。

  private def click(target = nil, similarity: nil, timeout: nil, offset: nil)
    Pikulix._click("click", target, similarity, timeout, offset, Pikulix::LEFT, 1)
  end

  private def double_click(target = nil, similarity: nil, timeout: nil, offset: nil)
    Pikulix._click("double_click", target, similarity, timeout, offset, Pikulix::LEFT, 2)
  end

  private def right_click(target = nil, similarity: nil, timeout: nil, offset: nil)
    Pikulix._click("right_click", target, similarity, timeout, offset, Pikulix::RIGHT, 1)
  end

  # マウスカーソルを対象の上へ動かす。
  private def hover(target, similarity: nil, timeout: nil, offset: nil)
    x, y = Pikulix._point("hover", target, similarity, timeout, offset)
    Pikulix._log "hover (#{x}, #{y})"
    Pikulix._mouse_move(x, y)
    Pikulix._sleep(Pikulix.delay)
    nil
  end

  # from から to へドラッグ＆ドロップする。
  private def drag_drop(from, to, similarity: nil, timeout: nil)
    Pikulix._drag_drop(from, to, similarity, timeout)
  end

  # ホイールを回す。wheel :down, 3 のように、向き（:up か :down）と回数を指定する。
  # 先頭に対象を書くと、そこへマウスカーソルを動かしてから回す。
  private def wheel(*args, similarity: nil, timeout: nil)
    Pikulix._wheel(args, similarity, timeout)
  end

  # --- キーボード ---

  # 文字列を入力する。キーボードの配列には依存しない。
  private def type(text)
    raise TypeError, "入力する文字列を指定してください: #{text.inspect}" unless text.is_a?(String)

    Pikulix._log "type #{text.inspect}"
    Pikulix._type(text)
    Pikulix._sleep(Pikulix.delay)
    nil
  end

  # キー操作を送る。key "ctrl+s" のように、同時に押すキーを + でつなぐ。
  # 複数を渡すと、順に送る。
  private def key(*combos)
    raise ArgumentError, "キーを指定してください（例: key \"ctrl+s\"）" if combos.empty?

    combos.each do |combo|
      raise TypeError, "キーは文字列で指定してください: #{combo.inspect}" unless combo.is_a?(String)

      Pikulix._log "key #{combo.inspect}"
      Pikulix._key(combo)
      Pikulix._sleep(Pikulix.delay)
    end
    nil
  end

  # クリップボード経由で文字列を貼り付ける（Ctrl+V）。type を受け付けないアプリで使う。
  private def paste(text)
    raise TypeError, "貼り付ける文字列を指定してください: #{text.inspect}" unless text.is_a?(String)

    Pikulix._log "paste #{text.inspect}"
    Pikulix._paste(text)
    Pikulix._sleep(Pikulix.delay)
    nil
  end

  # --- ファイル ---
  #
  # テキストファイルの読み書き。文字コードは UTF-8 で、パスは画像と同じく
  # スクリプトのあるフォルダからの相対で書く。

  # ファイルの中身を文字列で返す。
  private def read_text(path)
    Pikulix._check_path("read_text", path)
    Pikulix._read_text(path)
  end

  # 文字列をファイルに書く。無ければ作り、あれば上書きする。
  private def write_text(path, text)
    Pikulix._check_path("write_text", path)
    Pikulix._check_text("write_text", text)
    Pikulix._write_text(path, text, false)
  end

  # 文字列をファイルの末尾に書き足す。無ければ作る。
  private def append_text(path, text)
    Pikulix._check_path("append_text", path)
    Pikulix._check_text("append_text", text)
    Pikulix._write_text(path, text, true)
  end

  # ファイルがあれば true を返す。フォルダは false になる。
  private def file_exist?(path)
    Pikulix._check_path("file_exist?", path)
    Pikulix._file_exist(path)
  end

  # --- その他 ---

  private def sleep(seconds)
    unless seconds.is_a?(Numeric) && seconds >= 0
      raise ArgumentError, "sleep は 0 以上の秒数で指定してください: #{seconds.inspect}"
    end

    Pikulix._sleep(seconds.to_f)
    seconds.round
  end

  # アプリを起動する。ファイルや URL を渡すと、関連付けられたアプリで開く。
  private def run(command, *args)
    unless command.is_a?(String) && args.all? { |arg| arg.is_a?(String) }
      raise TypeError, "run は文字列で指定してください"
    end

    Pikulix._log "run #{([command] + args).map(&:inspect).join(', ')}"
    params = args.map { |arg| arg.include?(" ") ? "\"#{arg}\"" : arg }.join(" ")
    Pikulix._run(command, params)
    Pikulix._sleep(Pikulix.delay)
    nil
  end

  # スクリプトを終了する。status は終了コード（true は 0、false は 1）。
  private def exit(status = true)
    code =
      if status == true
        0
      elsif status == false
        1
      else
        status.to_i
      end
    raise SystemExit.new(code)
  end
end
