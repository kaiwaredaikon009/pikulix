# 画面の中から画像を探して、クリックする。
#
# 準備:
#   1. クリックしたいもの（ボタンやアイコン）を画面に出す。
#   2. Win+Shift+S を押し、その部分だけを四角く囲んで切り取る。
#   3. 切り取った画像を、このファイルと同じフォルダに target.png という名前で保存する。
#
# 実行:
#   pikulix.exe samples\click_image.rb
#
# 途中で止めたいときは、Alt+Shift+C を押す。

if exists("target.png", timeout: 5)
  click "target.png"
else
  # 見つからなかったときは、わざと find を呼んで失敗させる。そのときの画面が、
  # pikulix.exe のあるフォルダの logs に保存されるので、何が写っていたかを確かめられる。
  find "target.png", timeout: 0
end
