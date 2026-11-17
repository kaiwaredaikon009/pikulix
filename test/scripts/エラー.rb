puts "この行は実行される"

def fail_here
  # 次の行（5 行目）で例外を起こす。
  raise "わざと失敗させる"
end

fail_here
puts "この行は実行されない"
