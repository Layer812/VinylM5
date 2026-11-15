# VinylM5 — Conta microSD (10770)

VinylM5のM5Stack StopWatch向けmicroSD対応版です。スイッチサイエンス製のConta microSDカードソケット拡張用モジュールに合わせたSPI端子配置を使用します。

**対応製品：** [Conta™ microSDカードソケット拡張用モジュール（ピンヘッダなし）／スイッチサイエンス製品10770](https://www.switch-science.com/products/10770)

## 接続

電源を切った状態で、StopWatch背面6ピンEXT端子に接続します。製品10770はピンヘッダが付属しないモデルなので、別途正しい向きで取り付けてください。

| StopWatch EXT端子 | GPIO | Conta 10770 |
| --- | --- | --- |
| `3V3_L2` | 3.3 V | `3V3` |
| `MUX_IO_1` | GPIO43 | `SS` |
| `MUX_IO_2` | GPIO44 | `MOSI` |
| `G9` | GPIO9 | `MISO` |
| `G0` | GPIO0 | `SCK` |
| `GND` | GND | `GND` |

**端子の並び：`3V3 / SS / MOSI / MISO / SCK / GND`**

- **3.3 VとGNDの一致を必ず確認してください。** 5 V信号、電源ピン位置の異なるモジュールの直挿しはしないでください。
- **GPIO0は起動時の設定端子です。** Conta用のSCK出力への切り替えは起動後に行います。外部基板が起動時にGPIO0をLOWに固定すると起動できない場合があります。
- SPIのMISOとSCKは他の2種類のリーダーと異なります。**Conta用ビルドを通常配列のカードリーダーに接続しないでください。**
- microSDカードは**FAT32**を使用します。NTFS・exFATはこのアプリでは読み込めません。

## 操作

| 操作 | 動作 |
| --- | --- |
| 右ボタン1回 | 再生・一時停止 |
| 右ボタン2回 | 次の曲 |
| 左ボタン1回 | 次のフォルダ |
| 左ボタン2回 | 現在のフォルダでランダム選曲 |
| レコード盤をなぞる | DJスクラッチ |

大容量の曲一覧は再生開始後にバックグラウンドで読み込み、曲索引をキャッシュします。

## インストール（Windows / PlatformIO）

このフォルダをPCに保存し、M5Stack StopWatchを接続してください。COM13でない場合は末尾にポート名を指定できます。

```bat
call "RUN_VINYLM5_R16_CONTA10770_FIX4_APP_ONLY_COM13.cmd"
```

書き込みスクリプトは実機のパーティション表と起動APP領域を確認してから**APP領域のみ**を書き換えます。Bootloader、パーティション表、NVS、音楽データ、microSD内のファイルは消去しません。検証が成立しない場合は書き込みを停止します。接続中のシリアルモニターは終了してから実行してください。

従来のカードリーダー向けの配線・コードは、[メインREADMEのmicroSD接続説明](../../README.md#microsdカードの接続)を参照してください。

---

## English

**Hardware:** [Switch Science Conta™ microSD Card Socket Module (without pin headers), product 10770](https://www.switch-science.com/products/10770).

Connect to the StopWatch rear 6-pin EXT header with power off: **`3V3 / SS / MOSI / MISO / SCK / GND`** (GPIO43=SS, GPIO44=MOSI, GPIO9=MISO, GPIO0=SCK). Check the physical power and ground pins before connecting. GPIO0 is a boot-strapping pin: it is configured as SCK only after startup. Do not connect this variant to readers using the other two pin layouts. Use a **FAT32** card, not NTFS or exFAT. This product is supplied without pin headers.

**Controls:** Right single/double = play-pause / next track; left single/double = next folder / random track in the current folder. The player indexes remaining folders in the background.

**Windows installation:** Run `RUN_VINYLM5_R16_CONTA10770_FIX4_APP_ONLY_COM13.cmd` in this directory, with the serial monitor closed. Pass a COM port name as the first argument if necessary. Only the device-verified APP partition may be written; other partitions and microSD data are not erased.

See the [main repository README](../../README.md) for installation and standard microSD wiring.
