# PINMAP — T-Beam V1.2(AXP2101)と XIAO ESP32S3 のピン配置

調査日: 2026-10-13(T-Beam 未着。**実機では未確認**)。CLAUDE.md の「ピン番号は公式リポジトリの `utilities.h` と回路図から」に従い、推測は書かない。
確認度: **原文確認**=私が公式ファイルの原文を読んだ / **調査報告**=調査エージェントが公式資料から読み取った報告(私は原文未確認) / **確認できず**。

## 出典の版

| 対象 | 版 |
|---|---|
| LilyGo-LoRa-Series | コミット `1991b1ed70915d4f246b841f99f6027166ea7330`(2026-10-12)。タグは `v0.0.1` のみ |
| `utilities.h` の場所 | `examples/Factory/utilities.h`(リポジトリ直下にはない)。T-Beam のブロックは `#if defined(T_BEAM_SX1262) \|\| defined(T_BEAM_SX1276) \|\| ...`(89 行目〜)。**AXP192 版と AXP2101 版でピン定義は共通**。PMU の種類は実行時に `LoRaBoards.cpp` の `beginPower()` が判定する |
| 回路図 | `schematic/LilyGo_TBeam_V1.2-lastest.pdf`(PDF のテキスト層からの読み取り。図面の目視はしていない) |
| XIAO | arduino-esp32 `variants/XIAO_ESP32S3/pins_arduino.h`(master `90274561`)、platform-espressif32 `v6.13.0` の `boards/seeed_xiao_esp32s3.json`、Seeed Wiki 2 ページ |

## T-Beam V1.2

| 機能 | GPIO | 定義名 | 確認度 |
|---|---|---|---|
| LoRa SCK | 5 | `RADIO_SCLK_PIN` | 原文確認 |
| LoRa MISO | 19 | `RADIO_MISO_PIN` | 原文確認 |
| LoRa MOSI | 27 | `RADIO_MOSI_PIN` | 原文確認 |
| LoRa CS | 18 | `RADIO_CS_PIN` | 原文確認 |
| LoRa RST | 23 | `RADIO_RST_PIN` | 原文確認 |
| LoRa DIO0 | 26 | `RADIO_DIO0_PIN` | 原文確認 |
| LoRa DIO1 | 33 | `RADIO_DIO1_PIN` | 原文確認 |
| LoRa DIO2(SX1276/78) | 32 | `RADIO_DIO2_PIN` | 原文確認 |
| LoRa BUSY(SX1262 のみ) | 32 | `RADIO_BUSY_PIN` | 原文確認 |
| GPS: ESP32 の RX ← GPS の TX | 34 | `GPS_RX_PIN` | 原文確認 |
| GPS: ESP32 の TX → GPS の RX | 12 | `GPS_TX_PIN` | 原文確認 |
| GPS ボーレート | 9600 | `GPS_BAUD_RATE` | 原文確認 |
| I2C SDA / SCL(PMU・OLED 共有) | 21 / 22 | `I2C_SDA` / `I2C_SCL` | 原文確認 |
| PMU 割り込み | 35 | `PMU_IRQ` | 原文確認 |
| ユーザーボタン | 38 | `BUTTON_PIN` | 原文確認 |
| ユーザー LED | 4(**LOW で点灯**) | `BOARD_LED`、`LED_ON`=LOW | 原文確認 |
| AXP2101 の I2C アドレス | 0x34(AXP192 も同じ) | XPowersLib `AXP2101_SLAVE_ADDRESS` | 調査報告 |
| OLED(SSD1306) | I2C 0x3C | `LoRaBoards.cpp` | 調査報告 |

### 電源(AXP2101)— 調査報告

| 供給先 | ライン | 備考 |
|---|---|---|
| ESP32 本体 | DCDC1 | **触らない**(公式サンプルも `setProtectedChannel` で保護) |
| LoRa | ALDO2(3300mV) | |
| GPS | ALDO3(3300mV) | GNSS のバックアップ電源は VBACKUP |
| バッテリー電圧 | `enableBattVoltageMeasure()` の後 `getBattVoltage()`(mV) | 電池未接続だと 0、`getBatteryPercent()` は -1 |
| 充電設定(公式サンプル) | 500mA、目標 4.2V | 18650 の仕様に合わせて確認してから使う |
| 充電 LED | PMU 制御(青)。`setChargingLedMode(...)` | |
| ライブラリ | XPowersLib 0.2.6(`#define XPOWERS_CHIP_AXP2101` の後に `#include <XPowersLib.h>`) | |

### 確認できなかったこと

- **920MHz MIC 版の LoRa チップ型番**:商品ページの仕様欄は "SX1278 (433 MHz) / SX1276 (868/915/923 MHz)"。SX1276 の可能性が高いが断定できない。**実機のチップ表記で確認する**
- GPS の型番(NEO-6M / NEO-M8N など。個体差あり)、PPS の GPIO(`GPS_PPS_PIN` の定義なし)
- AXP2101 での OLED 電源ライン、V1.1(AXP192)と V1.2(AXP2101)の基板上での見分け方(代替: I2C スキャンと `getChipModel()`)
- 拡張ヘッダのシルク位置とピン番号の対応

### 資料の食い違い

- 公式 `t_beam_hw.md` の "GNSS TX=12 / RX=34" は、コードと回路図(ESP32 の TX=12、RX=34)と視点が逆。**コードを採用**
- 同じ表に DIO0(26)の行がない。コードと回路図にはある。**コードを採用**

### 公式サンプルをそのまま使ってはいけない点(コンプライアンス)

- 公式 SX1276 サンプルは `CONFIG_RADIO_FREQ 868.0`、`CONFIG_RADIO_OUTPUT_POWER 17`。**17dBm は上限(10、12 超は禁止)を超える。**
- 公式 `variants/tbeam-series/platformio.ini` の `[env:T_BEAM_SX1276_920MHZ]` は `CONFIG_RADIO_FREQ=920.0`。**証明書の範囲(920.6〜928.0MHz)の外。**
- 流用するのはピンの定義と初期化の手順だけ。周波数・出力は `docs/COMPLIANCE.md` の確認後に、`tx_guard.h` を通して設定する。

## UART(T-Beam ⇄ XIAO)の配線案

| 信号 | XIAO | T-Beam | 備考 |
|---|---|---|---|
| XIAO → T-Beam | D6(TX、GPIO43) | **GPIO13**(RX) | |
| T-Beam → XIAO | D7(RX、GPIO44) | **GPIO14**(TX) | |
| GND | GND | GND | 共通にする |
| 電源 | USB | USB | 別々に給電する前提 |

- T-Beam の GPIO13 / 14 は、GPS・PMU・LoRa・LED・ボタンとは衝突しない(`utilities.h` と回路図の報告による)。公式の TFT シールド設定が 13・14 を使っているが、内蔵部品との衝突は報告されていない。**ヘッダ上の位置(シルク)は実機で確認する**
- 使えないピン:GPIO16 / 17(PSRAM)。GPIO2 / 15(ブートストラップ)、GPIO36 / 39(入力専用)は避ける。`Serial2` の既定ピンは 16/17 と衝突するので、`begin(baud, SERIAL_8N1, rx, tx)` で明示する
- 電圧:T-Beam は +3V3 系。**XIAO の GPIO の論理レベルは Wiki に明記がなく、確認できなかった**(ESP32-S3 は一般に 3.3V 系だが、資料では未確認)。実機の接続前に確認する
- XIAO 側は `ARDUINO_USB_CDC_ON_BOOT=1` を維持する(`0` にすると `Serial`=UART0 が GPIO43/44 になり、D6/D7 の `Serial1` と取り合う)。D6 にブート時の ROM ログが出るかは**実機で確認する**
- ボーレートは 115200、8N1(ARCHITECTURE §5)

## XIAO ESP32S3 のピン

| 機能 | 値 | 確認度 |
|---|---|---|
| D0〜D10 → GPIO | 1, 2, 3, 4, 5, 6, **43**, **44**, 7, 8, 9 | 調査報告(Wiki とコードで一致) |
| UART | TX=D6(GPIO43)、RX=D7(GPIO44) | 同上 |
| I2C | SDA=D4(GPIO5)、SCL=D5(GPIO6) | 同上 |
| SPI | SCK=D8(GPIO7)、MISO=D9(GPIO8)、MOSI=D10(GPIO9) | 同上 |
| 制限ピン | GPIO0=ブート、GPIO39〜42=JTAG | Wiki |
| board 名 | `seeed_xiao_esp32s3`(board JSON が `ARDUINO_USB_CDC_ON_BOOT=1` を既に設定) | 調査報告 |
