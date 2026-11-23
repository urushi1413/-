# リリース用のファイル

## sweetspot-web.zip（ブラウザ版・スマホ対応）

`make mobile` で作った `web/mobile` の 4 つのファイル（index.html・index.js・index.wasm・index.data）をまとめたもの。
左上の版の表示は消してある（`game.h` の `SHOW_VERSION` 0）。

### itch.io に上げる

1. itch.io でページを作る（Create new project）
2. Kind of project：**HTML**
3. Uploads：`sweetspot-web.zip` を上げて「This file will be played in the browser」にチェック
4. Embed options：
   - Viewport dimensions：1280 × 720
   - **Mobile friendly** にチェック、Orientation は **Landscape**
   - **Fullscreen button** にチェック
5. Windows 版の zip（`tools/make_dist.ps1` で作る）も Uploads に足す（こちらはダウンロード用）
6. Visibility を **Draft** のまま保存し、スマホと PC で遊べるか確かめてから **Public** にする

### 作り直すとき

コードを変えたら `make mobile` で作り直し、`web/mobile` の 4 つをもう一度 zip にする。
