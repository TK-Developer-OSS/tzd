<!-- tzd.md v1 — TzD project rule file.  If present at the project root, cc1tzd/driver
     will read it automatically (task #36).  NOTE: the parser is not wired yet; until
     then this file is documentation and the compiler's built-in table is authoritative. -->

# TzD rules

This file holds the project's pointer-bounds rules, agreed with the developer
(interactively via #35 or by hand).  It is committed, so the build is reproducible.

## フォーマット定義（この節が本ファイルの書き方を定める。AI ごとに揺らさないこと）

1. **信頼される規則は ` ```tzd-rules ` フェンスの中だけ**に書く。コンパイラはフェンス
   内だけを読み、フェンス外の散文は一切信頼しない（根拠・履歴・メモ用）。
2. フェンス内は **1 行 1 規則**、`<対象> => @pointer <型>`。
   - **対象**:
     - 関数 … `strerror`（戻り値の規則）
     - フィールド … `struct hostent.h_name`
     - 変数 … `environ`
     - 不完全配列 … `_sys_siglist[]`
   - **型**（#34 の固定幅型とあわせる）:
     - 固定サイズ … `int8 int16 int32 int64`（符号なし `uint8…`）、名前付き型 `struct tm`、`T[N]`
     - `char[]` … NUL 終端文字列（長さは走査で決まる）
     - `T*[]` … NULL 終端ポインタ配列
     - `void` … 不透明（デリファイル不可・渡すだけ）
     - `[.n]` … 長さが第 n 引数（例 `char[.2]`）
3. **優先順位**: コンパイラ組み込みの既定表 < この `tzd.md` < 宣言直上の `//@pointer`（局所が勝つ）。
4. **host 戻り値の規則（関数対象）は信頼に敏感**。host が確保した領域の真のサイズを
   libc2d は照合できないため、広すぎる規則は過大読みを素通りさせる。よって関数対象の
   エントリは**人間がレビューしてから**追加し、行末に `# ok <名前/日付>` を付ける。
   利用者スロット由来の注釈は実行時に `tzd_sh_match` で照合されるのでこの限りではない。

## 規則（trusted — フェンス内のみ）

```tzd-rules
# まだ空。@pointer パーサ(#34)と自動参照(#36)の実装後に、現在 tzd-parse.cc の
# host_ptr_rules 表にある内容をここへ移していく。
```

## 根拠・履歴（散文 — 信頼されない）

- 2026-11-06: 雛形を作成（TK 指示: 冒頭に定義フォーマットを置き、AI ごとに書き方が
  揺れないようにする）。関連 task: #34（@pointer・固定幅型）, #35（MCP 連携）, #36（本ファイル）。
