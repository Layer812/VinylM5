# VinylM5 R12 — M5Unified / M5GFX 0.3.0 (COM16)

R11のスクラッチ・ループ再生・USB MP3転送・ID3/PNGラベル・音量操作を維持します。

## 変更
- M5Unified 0.2.25 → **0.3.0**
- M5GFX 0.2.33 → **0.3.0**（必ずM5Unifiedとセット）
- M5PM1 **1.0.7** / M5IOE1 **1.0.9** にバージョン固定
- PSRAMスプライトの転送後に `M5.Display.waitDMA()` を実行（DMA中の上書きを防止）
- 既存の `M5.begin(cfg)` 1回だけという初期化経路とStopWatchフォールバックを維持
- 書き込み用CMDの既定ポートを **COM16** に変更

## 書き込み
`RUN_VINYLM5_R12_BUILD_FLASH_COM16_APP_ONLY.cmd` を実行します。PlatformIOでビルドした後、COM16の実機からパーティション表とotadataを読み取り専用で照合し、一致するfactory APPにだけ書き込みます。MP3保存領域、パーティション、ブートローダーは変更しません。バックアップも不要です。

書き込みを行わない場合は `RUN_VINYLM5_R12_BUILD_ONLY.cmd` を実行してください。

## 状態
PCでの音声・スクラッチ・FAT16・ID3・ループ再生回帰テストはPASS。**ESP32-S3向けビルドと実機動作は未確認**です。画面が断続的に映らない症状の直接原因が、これらのライブラリだけと確定したわけではありません。既存のPlatformIO Arduino基盤は変更していないため、新しいDMA高速化がすべて有効になるとも限りません。

- [M5Unified 0.3.0](https://github.com/m5stack/M5Unified/releases/tag/0.3.0)
- [M5GFX 0.3.0](https://github.com/m5stack/M5GFX/releases/tag/0.3.0)


## StopWatch共通開発規約R1対応
- `-DBOARD_ID=30`を固定。M5.begin(cfg)一回の初期化とDMA同期は維持。
- `tools/flash_verified.py` で実機ESP32-S3、16MB Flash、BINヘッダー、APP容量、実機全パーティション、otadataの起動先を照合します。
- **共有されたOTA構成（ota_0/ota_1）ではこのfactoryビルドを拒否します。** このツールはotadata全消去状態だけをfactory起動が確定した状態と見なし、それ以外も中止します。otadataやNVSを変更しません。
- 書き込みCMDの入口は `VinylM5_R12_R1_SAFE/RUN_VINYLM5_R12_BUILD_FLASH_COM16_APP_ONLY.cmd`。
