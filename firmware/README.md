# firmware/

```
firmware/
├── tbeam/     T-Beam 用(LoRa・GPS・電池・ボタン、Wi-Fi/MQTT は出口役のとき)
├── xiao/      XIAO ESP32S3 用(GUIDE01 との BLE、T-Beam との UART)
└── shared/    共通コード(フレームの符号化・署名・重複排除)。PC 上でユニットテスト
```

## 方針

- `platformio.ini` は**雛形**。LILYGO の公式リポジトリ(LilyGo-LoRa-Series)と、Seeed の公式資料の設定に突き合わせて直す。
- T-Beam の役割(TERMINAL / RELAY / GATEWAY)は、ビルド設定または設定ファイルで切り替える。
- **`RERAY_TX_ENABLE` はデフォルト 0。** `docs/COMPLIANCE.md` の確認が終わるまで 1 にしない。
- Wi-Fi・MQTT の認証情報と署名鍵は `include/secrets.h`(git 管理外)。雛形は `secrets.example.h`。

## shared/ に置くもの

- フレームの構造体、符号化・復号(`docs/MESSAGE_SPEC.md`)
- HMAC-SHA256 による署名の生成・検証(ttl を除外)
- 重複排除のキャッシュ(`(src, seq, type)`、期限つき)
- 送信時間の予算管理(上限値は COMPLIANCE.md の確認結果から)
- 出力上限のクランプ(`RERAY_MAX_TX_DBM`)

ユニットテストは、PlatformIO の native 環境などで、PC 上で実行する。
