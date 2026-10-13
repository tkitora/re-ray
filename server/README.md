# server/

検証用のサーバー側。PC で動かす。

```bash
mosquitto -c server/mosquitto.conf -v
```

## トピック案

| トピック | 方向 | 内容(JSON) |
|---|---|---|
| `reray/up/report` | 出口 → PC | 受信した REPORT/SOS(src, seq, zone, 各区分, flags, time, rssi, snr, gw) |
| `reray/up/ack` | 出口 → PC | 受信した ACK(指示受領など) |
| `reray/down/instruct` | PC → 出口 | 送る指示(target_kind, target, code, param) |
| `reray/gw/status` | 出口 → PC | 出口の状態(Wi-Fi、LoRa、キューの数、電池) |

## 作るもの(Claude Code)

- 検証用のビューア(区域別の最新状況、時系列ログ、指示の送信、受領した端末と未受領の端末)。簡単な Python スクリプトや HTML で十分。
- ログの保存(CSV または SQLite)。`logs/` に出す。
- 区域の対応表(`zones.example.csv` を `zones.csv` にコピーして使う。端末番号 → 区域)。

## 注意

`allow_anonymous true` は検証専用。製品では認証と TLS を使う。
