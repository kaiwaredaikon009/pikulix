# Pikulix

画像一致で動くRPAツールである。
操作の手順は、Ruby（mruby）で書く。

*An English summary is [below](#english).*

```ruby
click "start_button.png"        # 画像を探して、その中心をクリックする
wait "dialog.png", timeout: 10  # 画像が現れるまで、最大 10 秒待つ
type "こんにちは"
key "ctrl+s"

if exists("error.png")
  click "ok.png"
end
```

## 特長

- **インストール不要。ランタイム不要。** Windows11があれば動く。
- レジストリや `%APPDATA%` を使用せず、**フォルダの外を汚さない**。
- **Ruby の文法で書ける**。変数、`if`、繰り返し、メソッドの定義、例外の処理が、そのまま使える。
- 実行時のキーボード配列や、日本語入力オンオフに左右されず、**書いたとおりに入力される**。
- **Alt+Shift+C を押すと、すぐに止まる。**

## 動く環境

- Windows 11（x64）

## はじめかた

1. [Releases](https://github.com/kaiwaredaikon009/pikulix/releases) から zip をダウンロードし、書き込みのできるフォルダに展開する。`C:\Program Files` の下は避ける。
2. 展開したフォルダをエクスプローラーで開き、アドレスバーに `cmd` と入力して Enter を押す。コマンドプロンプトが開く。
3. 次を実行する。メッセージボックスが表示されれば、動いている。

   ```
   pikulix.exe samples\hello.rb
   ```

zip の中身は次のとおりである。

| ファイル | 内容 |
| --- | --- |
| `pikulix.exe` | 本体 |
| `samples\` | サンプルスクリプト |
| `docs\api.md` | スクリプトの書き方（API の一覧） |
| `README.md` | この文書 |
| `LICENSE.txt`、`THIRD_PARTY_NOTICES.txt` | ライセンス |

### 「Windows によって PC が保護されました」と表示されたら

exe に電子署名が無いため、初回の実行時に SmartScreen の警告が出ることがある。「詳細情報」を押し、「実行」を押せばよい。

zip を展開する前に、zip のプロパティで「許可する」にチェックを入れておくと、この警告は出ない。

### ウイルス対策ソフトについて

画面を写し、マウスやキーボードを操作するプログラムは、ウイルス対策ソフトに誤検知されることがある。Pikulix はキーボードの監視などを使っていないが、それでも検知されることはありうる。気になるときは、ソースから自分でビルドできる（下の「ビルド」）。

## スクリプトを書く

1. 探したいボタンやアイコンを、画面から切り取って PNG で保存する。Snipping Tool（Win+Shift+S）が使える。
2. スクリプトを UTF-8 で書き、拡張子 `.rb` で保存する。画像のパスは、スクリプトのあるフォルダからの相対で書く。
3. `pikulix.exe スクリプト.rb` で実行する。スクリプトを `pikulix.exe` にドラッグ＆ドロップしてもよい。

主なメソッドは次のとおりである。

| 種類 | メソッド |
| --- | --- |
| 探す | `find`、`wait`、`exists`、`wait_vanish` |
| マウス | `click`、`double_click`、`right_click`、`hover`、`drag_drop`、`wheel` |
| キーボード | `type`（文字列の入力）、`key`（`"ctrl+s"` のようなキー操作）、`paste`（クリップボード経由の貼り付け） |
| ファイル | `read_text`、`write_text`、`append_text`、`file_exist?` |
| その他 | `sleep`、`run`（アプリの起動）、`message_box`、`exit` |

書式と指定できるもの（一致度のしきい値、待つ秒数、クリック位置のずらし方など）は、[docs/api.md](docs/api.md) にある。

### サンプル

| ファイル | 内容 |
| --- | --- |
| `samples\hello.rb` | メッセージボックスを表示する。 |
| `samples\notepad.rb` | メモ帳を開いて、文字を入力する。 |
| `samples\list_input.rb` | 一覧のファイル（`list.tsv`）を読んで、1 行ずつメモ帳に入力し、結果をファイルに書く。 |
| `samples\click_image.rb` | 自分で切り取った画像（`target.png`）を探して、クリックする。 |

`hello.rb` 以外は、実際にマウスやキーボードを動かす。実行中は、マウスやキーボードに触らない。

## 止めかた

**Alt+Shift+C** を押すと、すぐに止まる。どのウィンドウが前面にあっても効く。押したままのキーやマウスボタンは離される。

- Pikulix を 2 つ同時に動かさない。あとから起動したほうは、何もせずにエラーで終わる。
- 実行中、コンソールは最小化される。操作する相手を隠さないためで、終わると元に戻る。
- 終了コードは、成功が 0、スクリプトのエラーが 1、起動できなかったときが 2、Alt+Shift+C で止めたときが 3 である。

## うまくいかないとき

- **画像が見つからない**: 切り取ったときと実行するときとで、表示倍率・テーマ・フォントが違うと見つからない。見つからなかったときの画面が `logs` に保存されるので、確かめる。
- **間違ったものが見つかる**: `similarity:` でしきい値を上げるか、まわりを含めて大きめに切り取る。
- **クリックしても反応しない**: 管理者として動いているアプリには、入力が届かない。このときは警告が出る。管理者として開いたコマンドプロンプトから、Pikulix を実行する。

## 制約

- 探すのは、メイン ディスプレイだけである。
- ロック画面、切断したリモートデスクトップ、UAC の確認画面では動かない。
- 著作権保護された動画などは、黒く写ることがある。
- スクリプトは mruby で動く。CRuby の gem や標準ライブラリは使えず、`require`、`File`、`Dir` も無い。テキストファイルの読み書きは、`read_text` などで行う。

## SikuliX との関係

Pikulix は、SikuliX の考え方を参考にして作った、独立した実装である。SikuliX のコードは使っておらず、SikuliX の開発者とも関係が無い。スクリプトの書き方は似せてあるが、互換性は無い。

## ビルド

手順は [docs/ビルド手順.md](docs/ビルド手順.md) にある。RubyInstaller（Ruby+Devkit 版）があれば、`rake` だけでビルドできる。Visual Studio は要らない。

`docs/dev/` には、開発用の作業ノート（進み具合、設計と調査の記録、実機でのテストの手順）がある。使うだけなら、読む必要はない。

## ライセンス

MIT License である（[LICENSE.txt](LICENSE.txt)）。

pikulix.exe には、mruby（MIT License）と、gcc・MinGW-w64 のランタイムが組み込まれている。それらのライセンス表記は [THIRD_PARTY_NOTICES.txt](THIRD_PARTY_NOTICES.txt) にある。

---

## English

Pikulix is an image-matching RPA tool for Windows. You write the steps in Ruby (mruby).

### Features

- **No installer, no runtime.** Runs on a plain Windows 11 (x64).
- **Stays in its own folder.** No registry, no `%APPDATA%`. The only thing it creates is a `logs` folder next to the exe.
- **Text is typed as written,** regardless of keyboard layout or IME state.
- **Press Alt+Shift+C to stop.**

### Getting started

1. Download the zip from [Releases](https://github.com/kaiwaredaikon009/pikulix/releases) and extract it to a writable folder (not under `C:\Program Files`).
2. Open a command prompt in that folder and run `pikulix.exe samples\hello.rb`. A message box should appear.

To write your own script, capture the target as a PNG with the Snipping Tool (Win+Shift+S), write a `.rb` file in UTF-8, and run `pikulix.exe script.rb`. Image paths are relative to the script's folder.

The main methods are `find`, `wait`, `exists`, `wait_vanish`, `click`, `double_click`, `right_click`, `hover`, `drag_drop`, `wheel`, `type`, `key`, `paste`, `read_text`, `write_text`, `append_text`, `file_exist?`, `sleep`, `run`, `message_box` and `exit`. The full reference, [docs/api.md](docs/api.md), is in Japanese only.

### Notes

- The exe is not code-signed, so SmartScreen may warn you on the first run. Click "More info", then "Run anyway".
- It captures the screen and sends input, so some antivirus software may flag it. You can build it yourself instead (see [docs/ビルド手順.md](docs/ビルド手順.md), in Japanese).
- `docs/dev/` contains the working notes used during development (in Japanese). You do not need them to use Pikulix.
- Only the primary monitor is searched. It does not work on the lock screen, in a disconnected Remote Desktop session or on UAC prompts. Apps running as administrator do not receive its input unless Pikulix also runs as administrator.
- Pikulix is an independent implementation inspired by SikuliX. It contains no SikuliX code and is not affiliated with the SikuliX project. Scripts are not compatible.

### License

MIT License (see [LICENSE.txt](LICENSE.txt)). For the bundled third-party components (mruby, the GCC and MinGW-w64 runtimes), see [THIRD_PARTY_NOTICES.txt](THIRD_PARTY_NOTICES.txt).
