# StopWatch共通開発規約 R1 — VinylM5 R12適用
制定：2026-11-07。第1～13章を適用します。

- M5Unified 0.3.0、M5GFX 0.3.0、M5PM1 1.0.7、M5IOE1 1.0.9を固定。
- -DBOARD_ID=30、ESP32-S3R8、16MB Flash、8MB PSRAM。
- M5.begin(cfg)単一起動。GPIO46・AMOLED電源/リセットは触らない。
- 新M5GFXのPSRAM DMAを転送先 M5.Display.waitDMA() で保護。
- 左右物理ボタンはM5.BtnA/BtnB、タッチはM5.Touch.getDetail()。
- 実機パーティション表とotadataの読み取り専用照合、イメージヘッダー/サイズ照合。
- R12 factory / storage構成と一致しない機体、とくにOTA構成は書き込み中止。
- COM16既定、APPのみ。バックアップ、全消去、otadata変更はしない。

ZIP内の実行入口:
VinylM5_R12_R1_SAFE/RUN_VINYLM5_R12_BUILD_FLASH_COM16_APP_ONLY.cmd

ホストテストPASS / ESP32-S3向け実ビルド未確認 / R12実機未確認。
R11の確認済みファームおよびM5Burner公開ファームは維持します。
