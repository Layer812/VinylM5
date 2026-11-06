# VinylM5

M5Stack StopWatch用のレコード風MP3プレイヤーです。  
円形ディスプレイにレコード盤と曲のラベルを表示し、MP3を再生します。

**実機動作確認済み（R10）**：MP3再生、レコード操作、USB経由の楽曲転送を確認しました。

## インストール

**M5Burner シェアコード：** `rg69duyAcUQoohOl`

## 操作方法

| 操作 | 動作 |
| --- | --- |
| 右ボタン（1回） | 再生／一時停止 |
| 右ボタン（ダブルクリック） | 次の曲 |
| 左ボタン | 次のフォルダへ移動（ルートでは最初の子フォルダへ） |
| レコード盤を指で押さえて動かす | 停止・正転／逆転スクラッチ |
| 中央ラベルの上側をタップ | 音量アップ |
| 中央ラベルの下側をタップ | 音量ダウン |
| 中央の軸部分を長押し | USBドライブモード |

USBドライブモードでMP3を転送できます。PCでのコピー後は、安全に取り出してから再生に戻してください。

ラベルにはID3の曲名・アーティスト名・ジャケット画像を使用します。埋め込み画像がない場合は、同じフォルダで最初に見つかったPNG画像を使用します。日本語の曲名にも対応しています。

---

## English

VinylM5 is a vinyl-style MP3 player for the **M5Stack StopWatch**.  
It plays MP3 files while displaying a spinning vinyl record and track label on the round screen.

**Hardware-tested (R10):** MP3 playback, record controls, and USB music transfer have been confirmed.

## Installation

**M5Burner share code:** `rg69duyAcUQoohOl`

## Controls

| Control | Action |
| --- | --- |
| Right button (single click) | Play / pause |
| Right button (double click) | Next track |
| Left button | Next folder (first subfolder from root) |
| Touch and move the record | Hold and scratch forward / backward |
| Tap upper half of center label | Volume up |
| Tap lower half of center label | Volume down |
| Long-press the center spindle | USB drive mode |

Copy MP3 files using USB drive mode, then safely eject the drive on your PC before returning to playback.

The center label displays ID3 track information and embedded cover art. When no embedded artwork is available, the first PNG image found in the same folder is used. Japanese track titles are supported.
