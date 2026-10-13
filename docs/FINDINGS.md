# FINDINGS — p-02 検証結果

最終更新:
実施者:

## 環境

| 項目 | 内容 |
|---|---|
| T-Beam の基板表記(V1.2 / AXP2101 か) | |
| T-Beam の台数、ファームのバージョン | |
| XIAO ESP32S3 のファーム環境 | |
| GUIDE01 のファーム/版 | |
| GUIDE01 SDK のバージョン(タグ) | |
| PlatformIO、PC OS、Mosquitto の版 | |

## 判定表(企画書の主張ごと)

| 主張 | 結果(OK/NG/一部/未確認) | 備考・根拠(ログの場所) |
|---|---|---|
| LoRa で約600m届く(目標値) | | |
| 端末間リレーで2km規模でも届く(目標値) | | |
| 回線が落ちていても指示が届く | | |
| 受領確認で「届いたか」が分かる | | |
| ARグラスに文字で表示できる | | |
| ログが残り、後から見返せる | | |
| 電池で長く待機できる | | |

## 総合判定

(GO / 条件付き GO / 要再検討)と理由:

## S1:GUIDE01 の API 棚卸し

| 機能 | あり/なし/不明 | 関数名・クラス名 | 備考 |
|---|---|---|---|
| テキスト表示 | | | |
| 複数行表示 | | | |
| スクロール | | | |
| 画像・GIF | | | |
| タッチ/ジェスチャ通知 | | | |
| 表示クリア | | | |

表示の実測:1行の文字数、行数、日本語の可否:

## 測定値

| 測定 | 回数 | 条件 | 結果(平均/最大/到達率) | 方法・ログ |
|---|---|---|---|---|
| 2台の LoRa 送受信(S2) | | | | |
| A → C の到達率(S4) | | | | |
| 遅延(S4、S5) | | | | |
| 見通しでの最大到達距離(S7) | | | | |
| 市街地での到達(S7) | | | | |
| 人の多い環境での到達(S7) | | | | |
| 受信待ちの持続時間(S8) | | | | |

## テストごとの記録

### S0
### S1
### S2
### S3

**2026-10-13:`firmware/shared/` のうち PC で検証できる部分を実装(T-Beam 未着。実機の確認はまだ)**

- 実装:`sha256.h`(SHA-256/HMAC)、`frame.h`(符号化・復号・MIC・setTtl)、`dedup.h`(DedupCache、SeqTracker)、`airtime.h`(送信時間の計算、TxBudget)、`tx_guard.h`(電波法ガード)
- テスト:`make -C firmware/shared test`(clang 21、ASan+UBSan、`-Werror`)→ **216 チェック、失敗 0**。`make guard-test` で、`RERAY_MAX_TX_DBM=13`、`RERAY_TX_DBM > MAX`、`RERAY_TX_ENABLE=2` がそれぞれ static_assert で拒否されることを確認
- SHA-256/HMAC は自前実装。期待値は Python の `hashlib`/`hmac` で生成(RFC 4231 のケース1・2、ブロック長超の鍵、1000バイト細切れ入力)。一致を確認
- ttl が MIC に影響しないこと、ttl 以外のバイトを1bit変えると BadMic になること、u16 の seq 回り込みも確認
- 送信時間(Semtech の式、BW125/CR4/5/プリアンブル8/CRCあり、17バイト):**SF7=51.5ms、SF9=164.9ms、SF10=329.7ms**。MESSAGE_SPEC §8 の目安(約50/170/340ms)と整合。計算値であり、実機の測定ではない

**同日(追記):出口・状態遷移・リレー予約・UART を追加。テスト合計 675 チェック、失敗 0**(`make -C firmware/shared test`)。さらに `make json-check`(出口が出す JSON を Python で検証)と `make guard-test` も通過

- 追加:`node_id.h`(`dev-001` ↔ u16)、`gateway.h`(LoRa→signal/ack の JSON、トピック、サーバー到達 ACK、回線断キュー)、`report_tracker.h`(送信済み→近傍→サーバー到達/再送/未達、再送は 5→10→20 秒で最大 3 回)、`relay_scheduler.h`(優先度つきバックオフ、k=2 での抑制、満杯時の押しのけ)、`uart_proto.h`(行の形式、LOG の改行除去と UTF-8 を割らない切り詰め、行バッファ)
- テストの有効性:実装を 8 箇所わざと壊し(seq 比較、再送回数、抑制 k、UTF-8 切り詰め、キューの SOS 保護、ttl の署名除外、1 回の送信上限、ID の範囲)、**8 件全てテストが失敗した**ことを確認
- Drive 共通仕様との整合は `docs/HANDOFF_TO_COMMON_SPEC.md`(ドラフト)にまとめた。主な未決:下りの text を LoRa でどう運ぶか(INSTRUCT は 18 バイトで自由な文字は載らない)
- 仕様の変更案:REPORT の flags bit3〜7 に定型信号コード(Drive の 1〜4)を載せる、UART の STATE に FAIL を追加。どちらも MESSAGE_SPEC / ARCHITECTURE に反映済み(Drive の合意待ち)
- 注意:この結果は全て PC 上の確認。実機(LoRa の送受信、UART の配線、MQTT 接続)では未確認

仮置きした値(要確認):
- `kFrameVersion = 1`:MESSAGE_SPEC に値の指定がなかったため。0 は全ゼロの誤受信と区別できないので避けた
- 送信時間の予算(`BudgetConfig`):窓内上限 18 秒、1回の上限 1 秒、休止 100ms、REPORT は上限の 80%、BEACON は 50% まで。**規格で確認した値ではない**。COMPLIANCE.md の確認欄 #7 が埋まったら差し替える
- 未実装:キャリアセンス(無線チップの API が必要。T-Beam 実機の段階で実装)。リレーのバックオフ範囲(SF7 想定)は初期案で、S4 で実測して調整する

### S4
### S5
### S6
### S7
### S8

## 問題・未解決事項

| # | 内容 | 試したこと | 状態 |
|---|---|---|---|
| | | | |

## 次のアクション

- ユーザー確認待ち:(1) Drive 共通仕様とのずれ(LoRa パケット形式、MQTT トピック)の扱い、(2) PlatformIO のインストールと GUIDE01 SDK の clone の許可、(3) XIAO / GUIDE01 の手元有無
- T-Beam 不要で進められる残り:Drive への引き継ぎログの投稿(`docs/HANDOFF_TO_COMMON_SPEC.md` の確認後)、PlatformIO の native 環境の用意、実機用の HAL 層の設計(ピンは公式資料の確認後)
- GitHub への push:認証が `iruuRPG` で `tkitora/re-ray` に 403。コラボレーター追加か認証の切り替えが必要(コミット `fa8a0c5` はローカルにある)
