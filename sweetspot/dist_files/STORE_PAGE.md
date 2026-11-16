# SWEET SPOT — ストアページの文章と、販売までの手順

（このファイルは配る zip には入らない。ストアに貼る文章の下書き）

## タイトル

SWEET SPOT

## 短い紹介（itch.io の Short description / BOOTH の一行目）

- 日本語：打点と回転がものをいう、本物志向のテニスゲーム。足で打点に入り、タイミングとコースで勝負する。
- English: Real tennis where contact point and spin matter. Get your feet to the ball, time your swing, and place your shots.

## 説明文（日本語）

SWEET SPOT は、ボールを「どこで」「いつ」捉えるかがすべての、本物志向のテニスゲームです。

ボタンを押せば勝手に返る、のではありません。ボールの落ちる場所へ走り、ちょうどいい高さと距離に入り、いいタイミングで振る。芯で捉えた一球は、音も、球の伸びも違います。

■ 特徴
- 打点と回転のテニス：トップスピン・スライス・フラット・ロブ・ドロップを、マウスで狙って打ち分ける。長く溜めるほど深く、強く
- 本物らしいボール：回転で曲がり、弾み方はハード・クレー・芝で変わる。ネットの白帯をかすめるネットインも
- 個性の違う 16 人の選手：左利きの粘り屋、片手バックの万能型、ビッグサーバー……。打ち方も、足の速さも、勝負強さも違う
- グランドスラム：16 人の勝ち抜き。4 大会を続けて回るシーズンも。勝てば組み合わせ表が動き、優勝すればトロフィーを掲げる
- 試合の空気：観客の歓声とため息、主審のコール、線審の叫び、コートチェンジ、昼・夕方・ナイター、晴れ・くもり
- リプレイとハイライト：いいポイントはスローで見直せる。試合の後はハイライトを流す
- 練習モード：ラリー・リターン・サーブを好きなだけ
- 難しさは 2 段階（カジュアル・リアル）、AI の強さは 3 段階。はじめての人でも遊べる

■ 操作
マウスとキーボード（WASD で移動、マウスで狙う、クリックで打つ）。キーボードだけでも遊べる。キーの割り当ては自由に変えられる

## Description (English)

SWEET SPOT is a tennis game about *where* and *when* you hit the ball.

The ball doesn't come back just because you pressed a button. Run to where it lands, get to the right height and distance, and swing at the right moment. A shot struck in the sweet spot sounds different and flies differently.

FEATURES
- Contact-point tennis: aim with the mouse and choose topspin, slice, flat, lob or drop shot. Hold longer to hit deeper and harder
- A real ball: spin curves it, and it bounces differently on hard, clay and grass. Even net cords
- 16 players with distinct styles: a left-handed grinder, a one-handed all-courter, big servers… each with their own shots, speed and nerve
- Grand Slam: a 16-player knockout, or a season of four slams. Watch the draw unfold and lift the trophy
- Atmosphere: crowd cheers and groans, umpire calls, line judges, changeovers, day / evening / night sessions, sun and clouds
- Replays and highlights: watch your best points in slow motion
- Practice modes: rallies, returns and serves
- Two styles (Casual / Real) and three AI levels — friendly for beginners

CONTROLS
Mouse and keyboard (WASD to move, mouse to aim, click to hit). Keyboard-only play supported. All keys can be rebound

## タグの案

- itch.io：Sports, Tennis, Simulation, 3D, Singleplayer, Physics, Arcade
- BOOTH：テニス, スポーツゲーム, インディーゲーム, Windows

## 値段の目安

500〜800 円（itch.io なら 4〜6 ドル）。最初は安めか、発売セール。

## 動作環境

- Windows 10 / 11（64 ビット）
- OpenGL 3.3 に対応したグラフィック
- キーボード（マウスがあると遊びやすい）

## 載せないこと（大事）

ストアの説明・タグ・画像・動画・SNS の宣伝のどこにも、実在の選手名・大会名を書かない
（「〇〇みたいな選手が使える」も書かない）。

## 明日の手順

1. Windows で `git pull` → `make dist`
   - `dist\SweetSpot_Windows.zip` ができる（売る版の exe・音・フォント・クレジット・遊び方）
2. 別のパソコン（できれば開発をしていないもの）で zip を展開して、SweetSpot.exe が起動するか確かめる
   - 音が鳴るか、日本語が出るか、試合が最後までできるか
3. 宣伝用の画像と動画を撮る
   - ゲームの中で F12 を押すと、同じフォルダに screenshot000.png ができる（1280×720）
   - 動画は Windows の Xbox Game Bar（Win＋G）で録れる。15〜30 秒：芯で捉えた一球、長いラリー、ネットイン、優勝の表彰
4. itch.io
   - アカウントを作る → Upload new project → Kind of project：Downloadable
   - Pricing：Paid（例 5 ドル）。無料の体験版を同じページに置いてもいい
   - Uploads：SweetSpot_Windows.zip（Windows にチェック）
   - 説明文・画像・タグを上から貼る
   - 支払いの受け取り（PayPal か Stripe）と税金の書類（W-8BEN）を登録 → Public にして公開
5. BOOTH（日本向け）
   - pixiv アカウントでショップを開く → 商品の登録 → ダウンロード商品
   - zip を登録、値段、説明（日本語）、画像 → 公開。売上の受け取り口座も登録
6. X（Twitter）に動画と、ストアのリンクを投稿

## ブラウザ体験版とチャレンジ（無料の別ページ）

体験版はブラウザで遊べる版（`make web` で作る `web/out/`）。製品版とは別の、無料の itch.io ページに置く
（有料のページに置いたブラウザ版は、買った人しか遊べないことがあるため）。体験版のページから製品版のページへリンクする。

- 体験版でできること：試合（1 セットだけ）、練習、選手と試合の設定、キー設定。グランドスラムは製品版だけ
- 体験版ではスロー（T）と自動操作（B）は使えない（チャレンジのため）

### 体験版のページの文章

- 日本語：
  ブラウザですぐ遊べる SWEET SPOT の体験版です（1 セットの試合と練習）。
  【チャレンジ】AI の強さ「ふつう」以上・難しさ「リアル」で試合に勝つと、画面に「勝利の証明コード」が出ます。
  コードを（連絡先）に送ってくれた方に、製品版を無料でお渡しします（1 つのコードで 1 回。先着、予告なく終わることがあります）。
- English:
  Play SWEET SPOT in your browser (1-set matches and practice).
  CHALLENGE: beat the AI on NORMAL or PRO with the REAL style and you get a proof code on screen.
  Send the code to (contact) and get the full game free (one copy per code; may end without notice).

（連絡先）はメールアドレスか、受付用のフォームにする。コメント欄に書いてもらうと、ほかの人にコードを見られる。

### 体験版を itch.io に置く

1. `web/out/` の 4 つのファイル（index.html・index.js・index.wasm・index.data）を zip にする（フォルダごとではなく、中身を）
2. itch.io → Upload new project → Kind of project：HTML → Pricing：No payments
3. zip を上げて「This file will be played in the browser」にチェック
4. Embed options：Viewport 1280 × 720、Fullscreen button にチェック、Mobile friendly はなし（キーボードとマウスが要る）
5. Frame options の「SharedArrayBuffer support」は不要

### 届いたコードを確かめて、製品版の鍵を送る

1. `sweetspot/` で `python3 tools/check_code.py SS-XXXXX-XXXXX-XXXXX-XXXXXX`
   - `OK`：条件を満たした本物のコード。`web/used_codes.txt` に記録される
   - `使用済み`：前に受け付けたコード（使い回し）。`だめ`：打ち間違いか、作られたコード
2. itch.io の製品版のページ → Edit game → Distribute → Download keys → 鍵を作って、その人に送る
3. 鍵の確かめには `web/demo_secret.txt`（秘密の鍵）が要る。なくすと確かめられないので、パソコンの外にも保管する。
   リポジトリには入れない（公開しているので、入れるとコードを作られてしまう）
