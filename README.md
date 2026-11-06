# VinylM5

M5Stack StopWatch用のレコード風MP3プレイヤーです。  
円形ディスプレイに回転するレコードを表示し、MP3を再生します。

**現在開発・実機動作確認中です。**

## 操作方法

| 操作 | 動作 |
| --- | --- |
| 右ボタン（1回） | 再生／一時停止 |
| 右ボタン（ダブルクリック） | 次の曲 |
| 左ボタン | 次のフォルダへ移動（ルートでは最初の子フォルダへ） |
| レコードに指を置いて動かす | 停止・スクラッチ（正転／逆転） |

MP3はUSBドライブモードから転送する方式です（動作確認中）。  
中央ラベルには曲名やジャケット画像を表示します。ID3に画像がない場合は、同じフォルダのPNG画像を使用します。

---

## English

VinylM5 is a vinyl-style MP3 player for the **M5Stack StopWatch**.  
It plays MP3 files while showing a spinning record on the round display.

**Under development and hardware testing.**

### Controls

| Control | Action |
| --- | --- |
| Right button (single click) | Play / pause |
| Right button (double click) | Next track |
| Left button | Next folder (first subfolder when at the root) |
| Touch and move the record | Stop and scratch forward / backward |

MP3 files are transferred using USB drive mode (currently being tested).  
The center label displays track information and artwork. If no embedded ID3 artwork is available, a PNG image in the same folder is used.
