# server/

検証用の Mosquitto 設定だけを置く。**本物のサーバーは別リポジトリ**(`yuki20060410/Re-Ray_Server`、サーバー担当)。ここはハード単体の確認用。

```bash
mosquitto -c server/mosquitto.conf -v
```

## トピック(Drive 共通仕様「03_MQTTトピック」に従う)

`{id}` は端末ID(例 `dev-001`)。JSON の形は Drive の「02_メッセージ形式」。出口ノードが LoRa で受けた分は `via` が `lora`。

| トピック | 向き | 中身 | QoS |
|---|---|---|---|
| `reray/v1/up/{id}/signal` | 出口 → サーバー | signal(`x` に zone・RSSI などの追加項目を入れることがある【変更案】) | 1 |
| `reray/v1/up/{id}/status` | 出口 → サーバー | status | 0 |
| `reray/v1/up/{id}/ack` | 出口 → サーバー | ack(指示受領) | 1 |
| `reray/v1/down/{id}/text`、`reray/v1/down/all/text` | サーバー → 出口 | text | 1 |

LoRa で text をどう運ぶか(INSTRUCT は 18 バイトで自由な文字は載らない)は未決定。`docs/HANDOFF_TO_COMMON_SPEC.md` を参照。

## 作るもの(Claude Code)

- 検証用のビューア(区域別の最新状況、時系列ログ、指示の送信、受領した端末と未受領の端末)。簡単な Python スクリプトや HTML で十分。
- ログの保存(CSV または SQLite)。`logs/` に出す。
- 区域の対応表(`zones.example.csv` を `zones.csv` にコピーして使う。端末番号 → 区域)。

## 注意

`allow_anonymous true` は検証専用。製品では認証と TLS を使う。
