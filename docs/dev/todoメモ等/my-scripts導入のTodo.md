# my-scripts の追加にともなう Todo

開発者向けのメモ。`my-scripts/`（RPA スクリプトを書くためのディレクトリ）を足すにあたって、あとで手を入れるところをまとめた。

## 1. `my-scripts/` フォルダを作る

- [ ] リポジトリの直下に `my-scripts/` を作る。
- [ ] このメモのフォルダ（`docs/dev/todoメモ等/`）にある `AGENTS.md` と `CLAUDE.md` を、`my-scripts/` へ移す。どちらも `my-scripts/` に置く前提で書いてある。このメモ（`my-scripts導入のTodo.md`）は開発者向けなので、移さない。

## 2. `docs/api.md` の情報拡充および整理

いまの「使えないもの」には、gem、`require`、`File`、`Dir` が挙がっている。ほかにも、AI がふつうに書いてしまうのに、使えないものがある。

### やること

`Time#strftime` は使えるようにする（2026-11-19 に決めた）。理由は、下の「`Time#strftime` について確かめたこと」にある。

- [ ] `mruby_build_config.rb` に `conf.gem core: "mruby-strftime"`（mruby 4.0.0 に同梱の gem）を足す。
- [ ] `src/main.cpp` の `wmain` の最初で、`setlocale(LC_TIME, "en-US.UTF-8")` を呼ぶ（`<clocale>` が要る）。呼ばないと、書式に日本語が入ったときに、結果全体が空文字列になる。
- [ ] 使えない書式を渡したときに、空文字列を返さずにエラーにする。`src/prelude.rb` で `Time#strftime` を包み、書式が空でないのに結果が空なら `ArgumentError` にする。
- [ ] `test/` にテストを足す。日本語の入った書式（`"%Y年%m月%d日"`）がそのまま使えること、`%#d` で 0 を詰めないこと、使えない書式（`%-d`）がエラーになること。
- [ ] 下の「文案」で示された情報を `docs/api.md` に足して、整理した形で書き直す。
- [ ] 決めたこと（`Time#strftime` を使えるようにしたこと、その方法と理由、正規表現を入れないこと）を、`docs/dev/設計と調査の記録.md` に書く。

### `Time#strftime` について確かめたこと

作業用のコピーで gem を足してビルドし、試した（2026-11-19）。

- 依存する DLL は増えない。今の exe と、DLL の一覧が同じだった。この gem は C ランタイムの `strftime` を呼ぶだけで、その関数は今の exe でもすでに読み込まれている（`Time#to_s` が使っている）。
- exe は約 15 KB 大きくなる。`rake test` は通る。
- 何もしないと、書式に日本語が入ったときに、結果全体が空文字列になる（`"%Y年%m月%d日"` → `""`）。エラーにはならない。`%Z` は、Shift_JIS の「東京 (標準時)」を返す。C ランタイムのロケール（地域と文字コードの設定）が、既定では UTF-8 でないためである。
- `setlocale(LC_TIME, "en-US.UTF-8")` を呼ぶと、どちらも直る（`"2026年03月05日"`、`"東京 (標準時)"`）。`LC_TIME` だけを変えるので、`%a` や `%b` は英語のまま（`Thu`、`Mar`）。日本のロケール（`LC_ALL` を `".UTF-8"`）にすると、`%a` が「木」になるので使わない。`Time#to_s` の結果は変わらない。
- ロケールを変えても、Ruby 独自の書式（`%-d`、`%L`、`%N`、`%s`、`%P`、`%k`、`%l`、`%:z`、`%^a`、`%3N`）は使えない。エラーにならず、結果全体が空文字列になる（`"%Y年%-m月"` → `""`）。gem には `%-` をエラーにする処理があるが、Microsoft のコンパイラ（MSVC）でビルドしたときだけ有効で、gcc では効かない。
- 0 を詰めないときは、`%#d` のように `#` を付ければよい（`"%Y/%#m/%#d %#H:%M"` → `"2026/3/5 7:08"`）。
- `en-US.UTF-8` では、`%c`、`%x`、`%X` が米国の形になる（`"3/5/2026 7:08:09 AM"`、`"3/5/2026"`、`"7:08:09 AM"`）。Ruby では `"Thu Mar  5 07:08:09 2026"`、`"03/05/26"`、`"07:08:09"` である。

### 確かめたこと

Rakefile と同じ mruby 4.0.0（コミット `831da26`）を、`mruby_build_config.rb` と同じ gembox（stdlib、stdlib-ext、math、metaprog）で Linux 上にビルドし、`mruby` コマンドで試した。Pikulix の exe そのものでは、`Time#strftime` が `NoMethodError` になることだけを確かめた。`src/prelude.rb` と `src/api.cpp` に、これらを補う定義が無いことは確かめた。

| 書いたもの | 結果 | 代わりの書き方 |
| --- | --- | --- |
| 正規表現（`/,/`、`=~`、`split(/,/)` など） | `NameError`（`uninitialized constant Regexp`） | 文字列で書く。`split(",")`、`gsub("-", "_")`、`sub`、`tr`、`include?`、`start_with?` は使える |
| `String#match?`、`String#scan` | `NoMethodError` | `include?`、`index`、`split` |
| `String#encode` | `NoMethodError` | なし（文字列は UTF-8 のまま扱う） |
| `Time#strftime` | `NoMethodError`。`mruby-strftime` が gembox に入っていない | `format("%04d%02d%02d", t.year, t.month, t.day)`。`Time#to_s` は `"2026-11-19 08:23:32 +0900"` のような文字列を返す |
| `ENV`、`JSON` | `NameError` | なし |

正規表現は、文法エラーにはならない。その行を実行したときに、初めて `NameError` になる（`p :before; "a" =~ /b/` では、`:before` が出てから止まった）。RPA では、クリックや入力をいくつか済ませたあとで止まることになる。

### 正規表現を入れない理由

正規表現は入れず、使えないままにする（2026-11-19 に決めた）。

- mruby 4.0.0 には、正規表現の gem が同梱されていない。mruby の `doc/guides/language.md` にも、正規表現には外部の gem（`mruby-regexp-pcre` や `mruby-onig-regexp` など）が要ると書かれている。
- 外部の gem は、C の正規表現ライブラリ（PCRE、Onigmo など）を使う。そのソースを gem に同梱しているものもあるので、DLL が増えるとは限らない（確かめてはいない）。それでも、他所のコードを抱えることになる。ライセンス表記を足し、mruby 4.0.0 で動くかを確かめ、その後の更新にも付いていく必要がある。

### 文案

> - 正規表現は無い。`/,/` のように書くと、その行を実行したときにエラー（`NameError`）になる。文法の誤りとは違い、実行するまで分からない。文字列を分けたり置き換えたりするときは、`split(",")`、`gsub("-", "_")` のように文字列で書く。
> - `Time#strftime` では、Ruby 独自の書式（`%-d`、`%L`、`%N`、`%s`、`%P`、`%k`、`%l`、`%:z`、`%^a` など）は使えず、エラーになる。0 を詰めないときは、`%-d` ではなく `%#d` と書く（`t.strftime("%#m月%#d日")` → `"3月5日"`）。`%c`、`%x`、`%X` は、Ruby とは形が違う（`"3/5/2026 7:08:09 AM"` のような米国の形）。
> - このほか、`String#match?`、`String#scan`、`String#encode`、`ENV` も無い。

## 3. `docs/api.md` の「操作せずに試す（テストモード）」に足す

コードを読んで確かめた（実行はしていない）。`g_app.dry_run` を見ているのは、`src/input.cpp`（マウス、キーボード、クリップボード）と `src/api.cpp`（入力の準備、画面の取得と保存、`run`）だけである。次のものは、テストモードでも実際に行われる。

- `message_box`: 表示され、閉じるまでスクリプトが止まる。
- `sleep`、`wait 2`（数だけを渡した `wait`）: 実際に待つ。
- `write_text`、`append_text`: 実際にファイルを書く。`read_text` も実際に読む。

### 文案

> テストモードでも、`message_box` は表示され、閉じるまで止まる。`sleep` は実際に待ち、`write_text` などはファイルを実際に書く。

## 4. `.gitignore` に足す

`my-scripts/` には、作ったスクリプトや、画面から切り取った画像がたまる。公開リポジトリなので、個人の情報が写った画像をうっかりコミットしないように、`AGENTS.md` と `CLAUDE.md` のほかは無視する。

```
# 利用者がスクリプトを書く場所。AGENTS.md と CLAUDE.md のほかはコミットしない
/my-scripts/*
!/my-scripts/AGENTS.md
!/my-scripts/CLAUDE.md
```

`/my-scripts/`（ディレクトリそのもの）と書くと、`!` で戻せなくなる。中身を指す `/my-scripts/*` と書く。いまの `.gitignore` にこの 4 行を足し、`git add -A` で `AGENTS.md` と `CLAUDE.md` だけが追加されること（`.rb`、`.png`、下のディレクトリは無視されること）を、手元の git で確かめた。

## 5. そのほか

- [ ] `CLAUDE.md` の「ディレクトリ構成とビルド」の表に、`my-scripts/` の行を足す。
- [ ] 必要なら、README に一言足す（リポジトリを clone した人向け）。zip には入れないので、利用者向けの「はじめかた」とは分けたほうがよさそう。
- [ ] `my-scripts/AGENTS.md` は、`docs/api.md` の節の名前（「使えないもの」）を参照している。節の名前を変えたときは、あわせて直す。`CLAUDE.md` の作業ルールの「あわせて更新する」に足しておくとよいかもしれない。
- [ ] 3 を `docs/api.md` に足したあとも、`my-scripts/AGENTS.md` にあるテストモードの注意（`message_box` で止まる、など）は残してよい。AI が自分で実行するときに、知っておく必要があるためである。
