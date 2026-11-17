# 一覧のファイルを読んで、1 行ずつメモ帳に入力する。終わったら、結果をファイルに
# 書き残す。画像は使わない。
#
#   pikulix.exe samples\list_input.rb
#
# 一覧は、このファイルと同じフォルダの list.tsv（タブ区切りのテキスト）である。
# 1 行目は見出しで、2 行目からは「品名、数量、単価」が並んでいる。
# 結果は、同じフォルダの result.txt に書き足される。
#
# 実行中は、マウスやキーボードに触らないこと。
# 途中で止めたいときは、Alt+Shift+C を押す。

# 一覧を読む。行末の改行を落とし、空の行を飛ばして、タブで分ける。
rows = read_text("list.tsv").lines.map(&:chomp).reject(&:empty?).map { |line| line.split("\t") }
rows.shift # 1 行目は見出しなので使わない

run "notepad.exe"
sleep 2 # メモ帳が開いて、入力できるようになるのを待つ

total = 0
rows.each do |name, quantity, price|
  amount = quantity.to_i * price.to_i
  total += amount
  type "#{name} #{quantity} 個 × #{price} 円 = #{amount} 円\n"
end
type "合計 #{total} 円\n"

# 結果を書き残す。append_text は、ファイルが無ければ作り、あれば末尾に書き足す。
now = Time.now
stamp = format("%04d-%02d-%02d %02d:%02d:%02d", now.year, now.month, now.day, now.hour, now.min, now.sec)
append_text "result.txt", "#{stamp} #{rows.size} 件を入力しました（合計 #{total} 円）\n"
puts "result.txt に結果を書き足しました"
