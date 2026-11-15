SWEET SPOT の効果音フォルダ

ここに録音ファイル（.wav / .ogg / .mp3 / .flac）を置くと、ゲームで鳴ります。
ない音は鳴りません（ゲームはそのまま動きます）。
同じ名前に _2, _3, _4 を付けたファイルを置くと、毎回その中からランダムに選びます
（例：hit_good.wav, hit_good_2.wav, hit_good_3.wav）。

ファイル名（拡張子なし）と、鳴る場面：
  hit_good      芯で打った音（一番大事）
  hit_ok        まあまあの当たり（なければ hit_good）
  hit_bad       外した当たり（なければ hit_ok）
  hit_frame     フレームに当たった音（なければ hit_bad）
  hit_slice     スライス（なければ hit_ok）
  hit_voice_soft 体勢が崩れて取った音（ラケットでぎりぎり取って地面に当たる音と控えめな声）。飛びついたとき・手が届くぎりぎり（体から 1.8m 以上）で取ったときだけ
  grunt_pre     打つ前の声。打つ 0.235 秒前から鳴らし始め、そのまま hit_voice につながる
  hit_voice     打球音と声の続き。grunt_pre を鳴らしたときだけ使う
                声は強いストローク（自分は深く押したとき、AI は 6 割）とサーブだけ。スライス・ボレー・ネット前は声なし。
                声は一度に 1 人だけ（前の声が終わるまで次の声は始めない）
  serve         サーブ（なければ hit_good）
  bounce_hard   ハードコートのバウンド
  bounce_clay   クレーのバウンド（なければ bounce_hard）
  bounce_grass  芝のバウンド（なければ bounce_hard）
  net           ネットに当たった音
  whiff         空振り（風を切る音）
  squeak        ハードの靴の「キュッ」（_2〜_5 も）。滑るとき 1 回、球に寄って距離を詰めるとき 2〜3 回続けて（なければ slide）
  slide         クレー・芝で滑る音
  crowd         観客のざわめき（試合中ずっと繰り返す。数秒〜数十秒。crowd_2 などがあれば 1 回ごとに選び直す）
  applause      ポイント後の拍手（短め）
  applause_big  ウィナーや長いラリーの後の大きな拍手（なければ applause）
  call_fault    審判の「フォルト」のコール（サーブがフォルト・ダブルフォールトのとき）
  call_out      審判の「アウト」のコール（ラリー中の球がアウトになったとき）
  match_win     試合に勝ったときの大歓声（applause_big に重ねる）
  boo           観客のブーイング。ラケットを叩きつけた（2 回目からもっと大きく）・投げた・球をかっ飛ばした・審判に詰め寄ったとき
  ooh           観客の「おお〜」。15cm 以内の惜しいアウト、8 打以上のラリーの終わりのミス、ダブルフォールト
  racket_smash  ラケットを地面に叩きつける音（なければ鳴らさない）。約 1.2 秒おきに 5 回叩き、racket_smash, _2, _3 … を順に鳴らす（ほかの音の 2 倍の大きさ）
  racket_land   投げたラケットが地面に落ちて転がる音（今は入っていない。なければ racket_smash）

主審のスコアコール（ポイントの 1.1 秒後。サーバーの点を先に言う：「フィフティーン」「ラブ」）：
  call_love / call_15 / call_30 / call_40
  call_all                                 「〜オール」（同点のとき 15/30 のあとに続ける）
  call_deuce                               デュース
  call_adv_server / call_adv_receiver      アドバンテージ
  call_game_server / call_game_receiver    ゲームを取った（サーバー / レシーバー）
  call_set_server / call_set_receiver      セットを取った
  タイブレークは言わない。ない言葉は言わない（数字は 2 つともあるときだけ言う）。
  録音した声は python3 tools/umpire_pa.py <録音フォルダ> で、会場のスピーカーを通した音にして sounds/ に書き出せる

  celebrate     喜びの声（「カモン！」「バモス！」など）。大事なポイントを取ったとき
  angry         怒りの声。マレーがポイントを落としたとき、ジョコビッチがラケットを叩きつけたとき

選手ごとの声（あれば、その選手だけこちらを使う。なければ上の共通の声を、選手ごとに高さ・大きさを変えて鳴らす）：
  <名前>_grunt_pre / <名前>_hit_voice / <名前>_celebrate / <名前>_angry   （_2〜_6 も）
  名前：federerr, djokovik, nadar, murraye, you（自分）
  例：nadar_grunt_pre.wav, nadar_hit_voice.wav, nadar_celebrate.wav（バモス！）, murraye_angry.wav
  <名前>_serve_pre / <名前>_serve_voice   サーブの声（なければその選手のストロークの声）
  選手本人の録音では、打つ前の声（_pre）の長さだけ前から鳴らし始める（録音ごとに長さを変えてよい）。
  _pre と _voice は同じ録音の前半と後半で、同じ倍率で大きさをそろえて切る（打つ前の声を後から大きくしない）。
  _2, _3 … を置いたときは、打つ前と打った後を必ず同じ番号の組で鳴らす。
  切り分けは python3 tools/cut_voices.py <録音フォルダ>（打った瞬間の秒数は tools/cut_voices.py の表に書く）
  今入っている選手の声：alcarazz（ストローク 2 種類・サーブ）、zverew（ストローク 2 種類・サーブ）、djokovik（サーブ）
  共通の声（grunt_pre / hit_voice）はアルカラスの録音。本人の声がない選手は、これを選手ごとの高さで鳴らす

共通の声を使うときの選手ごとの違い（声の高さ・大きさ・声を出す割合）：
  federerr  少し高い・小さい・2 割（ほとんど声を出さない）
  djokovik  高め・ふつう・6.5 割（短く鋭い）
  nadar     低い・大きい・9 割（ほぼ毎回、低く長い）
  murraye   少し低い・ふつう・6 割

打球音・バウンド音は、音の頭の無音を切っておくこと（遅れて聞こえるため）。
使う音は、配布してよい許可（CC0 など）があるものだけにすること。出典は CREDITS.txt に書く。
