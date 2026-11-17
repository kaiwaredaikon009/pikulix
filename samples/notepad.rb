# メモ帳を開いて、文字を入力する。画像は使わない。
#
#   pikulix.exe samples\notepad.rb
#
# 実行中は、マウスやキーボードに触らないこと。
# 途中で止めたいときは、Alt+Shift+C を押す。

run "notepad.exe"
sleep 2 # メモ帳が開いて、入力できるようになるのを待つ

type "Pikulix から入力しています。\n"
type "日本語も、記号 (!\"#$%&'[]{}@) も、キーボードの配列に関係なく入力できます。\n"
paste "この行は、クリップボード経由で貼り付けました。"
key "enter"
key "ctrl+a" # すべて選択
