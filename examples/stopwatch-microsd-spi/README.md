# StopWatch microSD SPI reader example / 読み込みサンプル

VinylM5 R16で実機動作したmicroSD初期化・読み込み処理を、他のStopWatchアプリでも再利用できるよう整理した**独立サンプル**です。

For the different Switch Science Conta 10770 pin layout, see the [dedicated VinylM5 variant](../../variants/conta10770/README.md).

This **read-only standalone example** reproduces the rear EXT MUX control, two safe microSD SPI pin profiles, FAT32 mounting, and opening a music file. It is derived from the VinylM5 R16 SD implementation, for which the user confirmed music playback and faster startup on a real StopWatch.

**サンプルの範囲：** SDマウント後、最大8階層・最大4,096項目を探索し、最初のMP3/FLACファイルの**先頭16バイト**をシリアル出力します。音楽のデコード・再生、R16のFlashキャッシュ・バックグラウンド索引は含みません。

**別配列：** [スイッチサイエンス Conta microSD（製品10770）用のVinylM5ソース](../../variants/conta10770/README.md)も公開しています。ContaはこのサンプルのA/B配列とは異なり、専用ビルドを使用します。

## Pin mapping / ピン配置

電源を切ってから接続します。StopWatch背面6ピンEXTの3.3V側からGND側へ向かう配列です。

| StopWatch EXT | GPIO | microSD module A | microSD module B |
| --- | --- | --- | --- |
| 3V3_L2 | Power 3.3V | 3V3 | 3V3 |
| MUX_IO_1 | GPIO43 | CS | CS |
| MUX_IO_2 | GPIO44 | MOSI | CLK |
| G9 | GPIO9 | CLK | MOSI |
| G0 | GPIO0 | MISO | MISO |
| GND | GND | GND | GND |

- **A:** `3V3 / CS / MOSI / CLK / MISO / GND`
- **B:** `3V3 / CS / CLK / MOSI / MISO / GND`

実機接続写真：[装着時](../../docs/images/microsd-mounted.jpg) / [モジュール側面](../../docs/images/microsd-module.jpg)

**Safety / 注意：**

- **3V3とGNDの位置は必ず照合。** 電源ピンの並びはプログラムでは変更できません。配置の違うモジュールを直挿しせず、5Vも接続しないでください。
- **GPIO0はBOOTストラップ。** MISOの入力にしか使いません。SDモジュールが起動時にGPIO0をLOWへ固定する場合は接続しないでください。
- `M5.begin()` の**後**に、M5IOE1（I2C `0x4F`）のPYG1ビットだけをLOW/GPIO43-44側へ変更します。ほかのI/Oエキスパンダビット・AMOLEDリセット・電源制御は変更しません。
- AMOLEDのSPI2とは別の **SPI3/HSPI** を利用します。
- **4 MHz → 2 MHz → 1 MHz** を、上記A/B各配線で試します。GPIO総当たりを行いません。
- **FAT32**のみ。32GB FAT32 microSDでVinylM5本体は実機再生済みです。NTFS・exFATは対象外です。
- `SD.begin(..., false)`を使用し、**microSDのフォーマット・ファイル書き込みなし**。Bootloader・パーティション表・NVSも変更しません。

## Files / ファイル構成

- [src/main.cpp](src/main.cpp) — ボード初期化、IOE1/MUX、SPIピン配列、SDマウント、フォルダ探索、ファイル読み込み
- [platformio.ini](platformio.ini) — 独立したPlatformIOビルド設定

## Build only / ビルドのみ

リポジトリのルートでPlatformIOを実行してください。

```powershell
pio run -d examples/stopwatch-microsd-spi
```

このサンプルには**書き込みスクリプトを付属しません**。実機に書き込む場合は、StopWatch共通規約に従い、実機のパーティション表・otadata・書込先APP・BIN境界を読み取り専用で照合後、対象APP領域だけを書き換えてください。`erase_flash`、Bootloader/Partition/NVSの上書き、一般的なフルFlash書込は禁止です。

**Versions / バージョン：** M5Unified/M5GFX 0.3.0、M5PM1 1.0.7、M5IOE1 1.0.9、`BOARD_ID=30`。実機で再生成功したVinylM5 R16本体では0.2.xペアを使用していますが、この**独立サンプルは規約に合わせて0.3.0ペアに固定**しています。この独立版のESP32-S3ビルド・実機動作はまだ未確認であり、VinylM5本体の動作実績とは区別してください。

## Serial example / 出力例（参考・実測ログではありません）

```text
microSD: rear EXT MUX OK
microSD mounted: A: CS/MOSI/CLK/MISO @4000000 Hz (29819 MiB)
First track: /Album/example.mp3; 16 header bytes: ...
```

GPL-3.0-or-later — Copyright (C) 2026 Layer812
