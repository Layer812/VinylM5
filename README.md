# VinylM5

*English version below / 英語版は下部にあります → [English](#english)*

![VinylM5](vin.gif)

M5Stack StopWatch用のレコード風MP3プレイヤーです。  
円形ディスプレイに回転するレコードを表示し、MP3を再生します。

**動作風景：** [Xで実機の動作動画を見る](https://x.com/layer812/status/2118882192084607121)

## インストール

M5Burner シェアコード：`OPziEKkRxviwEEDH`  
WebBurner：**更新申請中**（[従来の公開版](https://burner.m5stack.com/PU4PNM)）

収録サンプル曲：**Neon Spinback**（Original）

## microSD読み込みサンプル

StopWatch背面のSPI接続例は[サンプルコード](examples/stopwatch-microsd-spi/README.md)で確認できます。VinylM5 R16本体はA・B・Conta 10770の**3種類の配列を自動判別**し、設定画面での選択は不要です。

## 操作方法

| 操作 | 動作 |
| --- | --- |
| 右ボタン（1回） | 再生／一時停止 |
| 右ボタン（ダブルクリック） | 次の曲 |
| 左ボタン（1回） | 次の曲入りフォルダへ移動 |
| 左ボタン（ダブルクリック） | 現在のフォルダ内でランダム選曲 |
| レコード盤を指で押さえて動かす | 停止・正転／逆転スクラッチ |
| 中央ラベルの上側をタップ | 音量アップ |
| 中央ラベルの下側をタップ | 音量ダウン |
| 中央の軸部分を長押し | USBドライブモード |

USBドライブモードでMP3を転送できます。PCでのコピー後は、安全に取り出してから再生に戻してください。

ラベルにはID3の曲名・アーティスト名・ジャケット画像を使用します。埋め込み画像がない場合は、同じフォルダで最初に見つかったPNG画像を使用します。日本語の曲名にも対応しています。

## microSDカードの接続

StopWatch背面の6ピンEXT端子に、**3.3V動作**のmicroSD SPIカードリーダーを接続します。

<img src="docs/images/microsd-mounted.jpg" alt="StopWatch背面にmicroSDカードリーダーを装着した写真" width="380">
<img src="docs/images/microsd-module.jpg" alt="microSDカードリーダーのピン側面写真" width="380">

**電源を切ってから接続してください。** 電源端子の`3V3_L2`と`GND`の位置が一致するカードリーダーのみ使用できます。5Vモジュールや電源配列が異なる基板は直挿ししないでください。

### 自動判別に対応する3種類のピン配列

いずれも3.3V側からGND側への並びです。

| 種類 | microSDリーダーのピン配列 |
| --- | --- |
| A | `3V3 / CS / MOSI / CLK / MISO / GND` |
| B | `3V3 / CS / CLK / MOSI / MISO / GND` |
| C | `3V3 / SS / MOSI / MISO / SCK / GND` |

**C配列の製品例：** [スイッチサイエンス Conta microSDカードソケット拡張用モジュール（製品10770）](https://www.switch-science.com/products/10770)

StopWatch側の信号は、CS/SS=GPIO43、GPIO44・GPIO9・GPIO0が残るMOSI・SCK・MISOに対応します。起動時にSDカードの応答を確認してから適切な配列でSPI通信を開始するため、ボタンや画面での切替操作は不要です。

GPIO0は起動ストラップ端子です。起動時に外部回路からLOWへ固定しないモジュールを使用してください。3種類の既知の配列で順番にマウントを試し、成功した配列を採用します。失敗時にはSPIの使用を終了してから次の配列に切り替えます。記載のないモジュールや不正な接続では、信号競合を完全には防げません。

microSDは**FAT32**を使用します。NTFS・exFATは読み込めません。多数のフォルダがある場合でも、前回選択したフォルダを優先して再生開始し、残りはバックグラウンドで読み込みます。

### ソースからインストール（Windows / PlatformIO）

統合版のソースを取得し、シリアルモニターを閉じてから次のAPP-ONLYスクリプトを実行してください。

```bat
call "RUN_VINYLM5_R16_SD_AUTO_APP_ONLY_COM13.cmd"
```

別のCOMポートを使う場合は末尾に `COM14` などを指定できます。起動APP領域を確認してからアプリケーションのみ書き込み、ブートローダー、パーティション表、音楽データは消去しません。


---

## English

VinylM5 is a vinyl-style MP3 player for the **M5Stack StopWatch**.  
It plays MP3 files while displaying a spinning vinyl record and track label on the round screen.

**Demo video:** [Watch VinylM5 running on real hardware on X](https://x.com/layer812/status/2118882192084607121)

### Installation

**M5Burner share code:** `OPziEKkRxviwEEDH`  
**WebBurner:** **Updated release pending review** ([previous published version](https://burner.m5stack.com/PU4PNM))

Included sample track: **Neon Spinback** (Original).

### microSD SPI code example

A [standalone SPI reader example](examples/stopwatch-microsd-spi/README.md) documents the StopWatch's rear connector. VinylM5 R16 itself automatically identifies all three supported reader layouts, including Conta 10770.

### Controls

| Control | Action |
| --- | --- |
| Right button (single click) | Play / pause |
| Right button (double click) | Next track |
| Left button (single click) | Next folder containing playable tracks |
| Left button (double click) | Random track within the current folder |
| Touch and move the record | Hold and scratch forward / backward |
| Tap upper half of center label | Volume up |
| Tap lower half of center label | Volume down |
| Long-press the center spindle | USB drive mode |

Copy MP3 files using USB drive mode, then safely eject the drive on your PC before returning to playback.

The center label displays ID3 track information and embedded cover art. When no embedded artwork is available, the first PNG image found in the same folder is used. Japanese track titles are supported.

### microSD connection

Connect a **3.3V microSD SPI reader** to the StopWatch's rear 6-pin EXT connector while the power is off. Match the physical `3V3_L2` and `GND` positions. Do not directly attach readers with different power pins or a 5V supply.

VinylM5 supports and **automatically detects** these three layouts (from 3.3V to GND):

| Type | Reader pins |
| --- | --- |
| A | `3V3 / CS / MOSI / CLK / MISO / GND` |
| B | `3V3 / CS / CLK / MOSI / MISO / GND` |
| C | `3V3 / SS / MOSI / MISO / SCK / GND` |

The [Switch Science Conta microSD socket module, product 10770](https://www.switch-science.com/products/10770), uses Type C.

The player tests the three listed SPI pin layouts and uses the first successful FAT32 mount, releasing the SPI pins after failed attempts. No user selection is needed. GPIO0 is a boot-strapping pin and must not be held LOW by the reader at reset. Only the listed layouts are supported; this does not guarantee safety with arbitrary third-party hardware.

Use a **FAT32** microSD card (not NTFS or exFAT). Playback starts from the previous folder while the remaining directory catalog is loaded in the background.

For a source installation, close the serial monitor and run `RUN_VINYLM5_R16_SD_AUTO_APP_ONLY_COM13.cmd` on Windows (append a COM port name when needed). The script verifies the current boot application and writes only the APP partition.


---

## ライセンス / License

ソースコードは **[GPL-3.0-or-later](LICENSE)** で公開しています。外部ライブラリのライセンスは [THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md) を参照してください。カバー画像 `vin.gif` はソースコードのライセンス対象外です。

The source code is licensed under **[GPL-3.0-or-later](LICENSE)**. See [third-party notices](THIRD_PARTY_LICENSES.md) for library licenses. Cover artwork `vin.gif` is not covered by the source-code license.
