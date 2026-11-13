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
  squeak        ハードで靴がこすれる音
  slide         クレー・芝で滑る音
  crowd         観客のざわめき（試合中ずっと繰り返す。数秒〜数十秒）
  applause      ポイント後の拍手（短め）
  applause_big  ウィナーや長いラリーの後の大きな拍手（なければ applause）

打球音・バウンド音は、音の頭の無音を切っておくこと（遅れて聞こえるため）。
使う音は、配布してよい許可（CC0 など）があるものだけにすること。
