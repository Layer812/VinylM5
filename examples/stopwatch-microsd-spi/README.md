# StopWatch microSD SPI reader example / 読み込みサンプル

この読み取り専用サンプルも、VinylM5本体と同じ**公式C型／互換B・A型の起動時切替と記憶**に対応しています。

The standalone read-only example follows the same **official C / persistent B-A compatibility** policy as the main VinylM5 app.

## 起動時の操作 / Boot mode

| 操作 | モード | 保存 |
| --- | --- | --- |
| 通常起動（初回） | **C型のみ（公式対応）** | 標準値 |
| **青ボタン（右／BtnB）を押したまま起動** | **B→Aを順番に試す**（C型は試さない） | B/AモードをNVSに保存 |
| 次回以降の通常起動 | 保存したグループのみ | 維持 |
| **黄色ボタン（左／BtnA）を押したまま起動** | **C型のみ** | Cモードに戻して保存 |

両ボタン同時押しでは黄色を優先します。保存先：`vinyl_sd_hw` / `mode`（`0`=C、`1`=B/A）。この設定は本体とサンプルで共有します。

## SPI接続 / Pin mapping

StopWatch背面6ピンEXT（3.3V側からGND側への順序）。電源を切ってから接続してください。

| StopWatch EXT | GPIO | C（公式） | B（互換） | A（互換） |
| --- | --- | --- | --- | --- |
| 3V3_L2 | 3.3V | 3V3 | 3V3 | 3V3 |
| MUX_IO_1 | GPIO43 | SS | CS | CS |
| MUX_IO_2 | GPIO44 | MOSI | CLK | MOSI |
| G9 | GPIO9 | MISO | MOSI | CLK |
| G0 | GPIO0 | SCK | MISO | MISO |
| GND | GND | GND | GND | GND |

- **C（公式）**：`3V3 / SS / MOSI / MISO / SCK / GND` — [Conta 10770](https://www.switch-science.com/products/10770)
- **B（互換）**：`3V3 / CS / CLK / MOSI / MISO / GND`
- **A（互換）**：`3V3 / CS / MOSI / CLK / MISO / GND`

写真：[装着時](../../docs/images/microsd-mounted.jpg) / [モジュール側面](../../docs/images/microsd-module.jpg)

## Safety / 注意

- **3V3とGNDの一致を確認**。5Vモジュールや異なる電源配列を直挿ししないでください。
- **GPIO0はBOOTストラップ**。起動時にLOWへ固定しないでください。C型では`M5.begin()`後だけSCK出力になります。
- BとAはMISOがGPIO0で共通の入力、CLKとMOSIだけが入れ替わります。**MISOの位置が異なるC型とB/A型を連続試行しません。** 任意の未対応基板の安全性までは保証しません。
- `M5.begin()`後にM5IOE1 `0x4F` のPYG1だけをLOW/GPIO43-44へ切り替えます。AMOLEDリセット・電源制御には触れません。SDはSPI3/HSPIを使用します。
- SPIは選択グループ内で**4MHz → 2MHz → 1MHz**の順に試行します。失敗時はCSをHIGHにし、SPIを終了して3信号を入力に戻します。
- FAT32専用です。**microSDのフォーマット・ファイル書き込みはしません**。NVSに保存するのはモード選択のみです。

## サンプルの機能 / Behavior

SDマウント後に最大8階層・4,096項目を探索し、最初のMP3/FLACの先頭16バイトをシリアル出力します。再生機能や曲索引キャッシュは含みません。

[src/main.cpp](src/main.cpp) / [platformio.ini](platformio.ini)

```powershell
pio run -d examples/stopwatch-microsd-spi
```

**ビルドのみ。** 実機に書く場合は既存のAPP-ONLY安全規約（起動スロット・パーティション表・サイズ事前照合）を守ってください。`erase_flash`やNVS/Partition/Bootloaderの上書きは禁止です。

本サンプルはM5Unified/M5GFX 0.3.0、M5PM1 1.0.7、M5IOE1 1.0.9、BOARD_ID=30です。本体のライブラリ構成とは異なるため、サンプルの実機検証は別途必要です。

### Serial example（想定出力・実測ではありません）

```text
microSD: mode=C/OFFICIAL blue=0 yellow=0
microSD: rear EXT MUX OK
microSD mounted: C: SS/MOSI/MISO/SCK @4000000 Hz (29819 MiB)
First track: /Album/example.mp3; 16 header bytes: ...
```

GPL-3.0-or-later — Copyright (C) 2026 Layer812
