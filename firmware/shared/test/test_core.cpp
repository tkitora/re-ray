// 署名・フレーム・重複排除・送信時間・電波法ガードのテスト。
#include <stdio.h>
#include <string.h>

#include "reray/airtime.h"
#include "reray/dedup.h"
#include "reray/frame.h"
#include "reray/sha256.h"
#include "reray/tx_guard.h"
#include "harness.h"

using namespace reray;

static void hex(const uint8_t* p, size_t n, char* out) {
  for (size_t i = 0; i < n; i++) snprintf(out + 2 * i, 3, "%02x", p[i]);
}
static bool hexEq(const uint8_t* p, size_t n, const char* expect) {
  char buf[129];
  hex(p, n, buf);
  return strcmp(buf, expect) == 0;
}

static const uint8_t KEY[16] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};

// ---- SHA-256 / HMAC(期待値は Python の hashlib/hmac で生成) ----
static void test_sha_hmac() {
  puts("sha256/hmac");
  uint8_t d[32];
  Sha256 s;
  s.update((const uint8_t*)"abc", 3);
  s.final(d);
  CHECK(hexEq(d, 32, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));

  s.reset();
  s.final(d);
  CHECK(hexEq(d, 32, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));

  // ブロック境界をまたぐ入力(1000バイト、細切れに投入)
  uint8_t a[1000];
  memset(a, 'a', sizeof a);
  s.reset();
  for (size_t i = 0; i < sizeof a; i += 7) s.update(a + i, (sizeof a - i) < 7 ? (sizeof a - i) : 7);
  s.final(d);
  CHECK(hexEq(d, 32, "41edece42d63e8d9bf515a9ba6932e1c20cbc9f5a5d134645adb5db1b9737ea3"));

  uint8_t k1[20];
  memset(k1, 0x0b, sizeof k1);
  hmacSha256(k1, sizeof k1, (const uint8_t*)"Hi There", 8, d);
  CHECK(hexEq(d, 32, "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7"));

  const char* m2 = "what do ya want for nothing?";
  hmacSha256((const uint8_t*)"Jefe", 4, (const uint8_t*)m2, strlen(m2), d);
  CHECK(hexEq(d, 32, "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843"));

  uint8_t k3[131];  // ブロック長より長い鍵
  memset(k3, 0xaa, sizeof k3);
  const char* m3 = "Test Using Larger Than Block-Size Key - Hash Key First";
  hmacSha256(k3, sizeof k3, (const uint8_t*)m3, strlen(m3), d);
  CHECK(hexEq(d, 32, "60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54"));
}

// ---- フレーム ----
static Frame sampleReport() {
  Frame f;
  f.type = Type::Report;
  f.ttl = 3;
  f.src = 0x0102;
  f.seq = 0xFFFE;
  f.zone = 5;
  f.people = 4;
  f.injured = 2;
  f.rescue = 1;
  f.needs = 0x0B;
  f.flags = kFlagConfirmed | kFlagGpsValid;
  f.time = 1791849600;
  return f;
}

static void test_frame_roundtrip() {
  puts("frame roundtrip");
  uint8_t buf[kMaxFrameLen];

  Frame r = sampleReport(), o;
  CHECK(encodeFrame(r, KEY, sizeof KEY, buf) == 17);
  CHECK(decodeFrame(buf, 17, KEY, sizeof KEY, o) == DecodeStatus::Ok);
  CHECK(o.type == Type::Report && o.ttl == 3 && o.src == 0x0102 && o.seq == 0xFFFE);
  CHECK(o.zone == 5 && o.people == 4 && o.injured == 2 && o.rescue == 1 && o.needs == 0x0B);
  CHECK(o.flags == (kFlagConfirmed | kFlagGpsValid) && o.time == 1791849600);

  Frame sos = sampleReport();
  sos.type = Type::Sos;
  sos.flags |= kFlagSos;
  CHECK(encodeFrame(sos, KEY, sizeof KEY, buf) == 17);
  CHECK(decodeFrame(buf, 17, KEY, sizeof KEY, o) == DecodeStatus::Ok && o.type == Type::Sos);
  CHECK(o.flags & kFlagSos);

  Frame ins;
  ins.type = Type::Instruct;
  ins.ttl = 5;
  ins.src = 0;
  ins.seq = 7;
  ins.targetKind = 1;
  ins.target = 0x0203;
  ins.code = 5;
  ins.param = 9;
  ins.time = 123456789;
  CHECK(encodeFrame(ins, KEY, sizeof KEY, buf) == 18);
  CHECK(decodeFrame(buf, 18, KEY, sizeof KEY, o) == DecodeStatus::Ok);
  CHECK(o.targetKind == 1 && o.target == 0x0203 && o.code == 5 && o.param == 9 && o.time == 123456789);

  Frame ack;
  ack.type = Type::Ack;
  ack.ttl = 2;
  ack.src = 100;
  ack.seq = 1;
  ack.refSrc = 0x0102;
  ack.refSeq = 0xFFFE;
  ack.ackKind = (uint8_t)AckKind::Server;
  CHECK(encodeFrame(ack, KEY, sizeof KEY, buf) == 14);
  CHECK(decodeFrame(buf, 14, KEY, sizeof KEY, o) == DecodeStatus::Ok);
  CHECK(o.refSrc == 0x0102 && o.refSeq == 0xFFFE && o.ackKind == 2);

  Frame b;
  b.type = Type::Beacon;
  b.src = 3;
  b.seq = 9;
  b.lat = 3568123;   // 35.68123
  b.lon = -13976543; // 負値の往復確認
  b.batt = 87;
  b.role = 2;
  CHECK(encodeFrame(b, KEY, sizeof KEY, buf) == 19);
  CHECK(decodeFrame(buf, 19, KEY, sizeof KEY, o) == DecodeStatus::Ok);
  CHECK(o.lat == 3568123 && o.lon == -13976543 && o.batt == 87 && o.role == 2);
}

static void test_frame_layout() {
  puts("frame layout (MESSAGE_SPEC)");
  uint8_t buf[kMaxFrameLen];
  Frame r = sampleReport();
  encodeFrame(r, KEY, sizeof KEY, buf);
  CHECK(buf[0] == ((kFrameVersion << 6) | (0 << 3) | 3));
  CHECK(buf[1] == 0x01 && buf[2] == 0x02);  // src ビッグエンディアン
  CHECK(buf[3] == 0xFF && buf[4] == 0xFE);  // seq
  CHECK(buf[5] == 5 && buf[6] == 0x42 && buf[7] == 0x1B && buf[8] == 0x06);
  CHECK(buf[9] == 0x6A && buf[10] == 0xCD && buf[11] == 0x74 && buf[12] == 0x80);  // 1791849600 = 0x6ACD7480
}

static void test_frame_security() {
  puts("frame security");
  uint8_t buf[kMaxFrameLen], tmp[kMaxFrameLen];
  Frame r = sampleReport(), o;
  size_t n = encodeFrame(r, KEY, sizeof KEY, buf);

  // ttl を書き換えても MIC は有効(中継ノードが ttl を減らして再送できる)
  memcpy(tmp, buf, n);
  setTtl(tmp, 0);
  CHECK(decodeFrame(tmp, n, KEY, sizeof KEY, o) == DecodeStatus::Ok && o.ttl == 0);
  setTtl(tmp, 7);
  CHECK(decodeFrame(tmp, n, KEY, sizeof KEY, o) == DecodeStatus::Ok && o.ttl == 7);

  // ttl 以外のどのバイトを1bit変えても BadMic(MIC 自身も含む)
  for (size_t i = 0; i < n; i++) {
    for (int bit = 0; bit < 8; bit++) {
      memcpy(tmp, buf, n);
      tmp[i] ^= (uint8_t)(1 << bit);
      if (i == 0 && bit < 3) continue;  // ttl は対象外
      if (i == 0) continue;             // ver/type の変化は別の理由で弾かれうる
      CHECK(decodeFrame(tmp, n, KEY, sizeof KEY, o) == DecodeStatus::BadMic);
    }
  }

  // 鍵が違う
  uint8_t other[16] = {};
  CHECK(decodeFrame(buf, n, other, sizeof other, o) == DecodeStatus::BadMic);

  // 構造の異常
  CHECK(decodeFrame(buf, 3, KEY, sizeof KEY, o) == DecodeStatus::TooShort);
  CHECK(decodeFrame(buf, n - 1, KEY, sizeof KEY, o) == DecodeStatus::BadLength);
  memcpy(tmp, buf, n);
  tmp[0] = (uint8_t)((tmp[0] & 0x3F) | (2 << 6));
  CHECK(decodeFrame(tmp, n, KEY, sizeof KEY, o) == DecodeStatus::BadVersion);
  memcpy(tmp, buf, n);
  tmp[0] = (uint8_t)((tmp[0] & 0xC7) | (6 << 3));
  CHECK(decodeFrame(tmp, n, KEY, sizeof KEY, o) == DecodeStatus::BadType);

  // 不正な入力で出力を壊さない
  Frame keep = sampleReport();
  keep.zone = 99;
  tmp[0] = 0;
  decodeFrame(tmp, n, KEY, sizeof KEY, keep);
  CHECK(keep.zone == 99);
}

static void test_frame_encode_limits() {
  puts("frame encode limits");
  uint8_t buf[kMaxFrameLen];
  Frame r = sampleReport();
  r.ttl = 8;
  CHECK(encodeFrame(r, KEY, sizeof KEY, buf) == 0);
  r = sampleReport();
  r.people = 16;
  CHECK(encodeFrame(r, KEY, sizeof KEY, buf) == 0);
  r = sampleReport();
  r.needs = 16;
  CHECK(encodeFrame(r, KEY, sizeof KEY, buf) == 0);
}

// ---- 重複排除 ----
static void test_dedup() {
  puts("dedup");
  DedupCache<4> c(1000);
  CHECK(c.observe(1, 10, 0, 0) == 1);
  CHECK(c.observe(1, 10, 0, 100) == 2);
  CHECK(c.observe(1, 10, 0, 200) == 3);
  CHECK(c.heardCount(1, 10, 0, 300) == 3);
  CHECK(c.observe(1, 10, 1, 300) == 1);  // type が違えば別物
  CHECK(c.observe(2, 10, 0, 300) == 1);  // src が違えば別物
  CHECK(c.observe(1, 11, 0, 300) == 1);  // seq が違えば別物
  CHECK(c.observe(1, 10, 0, 1000) == 1); // 期限切れ(最初の観測から 1000ms)
  CHECK(c.heardCount(9, 9, 0, 1000) == 0);

  // 満杯になったら一番古いものを追い出す(新しいものは残る)
  DedupCache<2> d(100000);
  d.observe(1, 1, 0, 0);
  d.observe(1, 2, 0, 10);
  d.observe(1, 3, 0, 20);  // (1,1) が追い出される
  CHECK(d.heardCount(1, 1, 0, 30) == 0);
  CHECK(d.heardCount(1, 2, 0, 30) == 1);
  CHECK(d.heardCount(1, 3, 0, 30) == 1);

  // millis() の回り込み
  DedupCache<2> w(1000);
  w.observe(1, 1, 0, 0xFFFFFF00u);
  CHECK(w.observe(1, 1, 0, 0x00000064u) == 2);  // 経過 356ms
}

static void test_seq_tracker() {
  puts("seq tracker");
  SeqTracker<4> t;
  CHECK(t.accept(1, 100));
  CHECK(t.accept(1, 101));
  CHECK(t.accept(1, 101));   // 同じ seq は通す(重複は DedupCache の役目)
  CHECK(!t.accept(1, 100));  // 古い
  CHECK(t.accept(2, 5));     // 送信元ごとに別管理

  CHECK(t.accept(3, 65535));
  CHECK(t.accept(3, 0));     // 回り込み: 65535 → 0 は新しい
  CHECK(t.accept(3, 2));
  CHECK(!t.accept(3, 65535));// 回り込み後の 65535 は古い
}

// ---- 送信時間 ----
static void test_airtime() {
  puts("airtime");
  // MESSAGE_SPEC §8 の目安(SF7=約50ms, SF9=約170ms, SF10=約340ms)と、Semtech の式による値
  uint32_t t7 = loraAirtimeUs(17, 7), t9 = loraAirtimeUs(17, 9), t10 = loraAirtimeUs(17, 10);
  printf("  17B: SF7=%uus SF9=%uus SF10=%uus\n", t7, t9, t10);
  CHECK(t7 > 50000 && t7 < 53000);
  CHECK(t9 > 160000 && t9 < 175000);
  CHECK(t10 > 320000 && t10 < 345000);
  CHECK(loraAirtimeUs(19, 7) > loraAirtimeUs(14, 7));  // 長いほど遅い
  CHECK(loraAirtimeUs(17, 5) == 0);                    // 不正な SF
}

static void test_budget() {
  puts("tx budget");
  BudgetConfig cfg;
  cfg.windowMs = 10000;
  cfg.windowLimitMs = 1000;
  cfg.singleMaxMs = 400;
  cfg.minGapMs = 100;
  TxBudget<8> b(cfg);

  CHECK(!b.canSend(Priority::Sos, 500000, 0));  // 1回の上限(400ms)超え
  CHECK(!b.canSend(Priority::Sos, 0, 0));
  CHECK(b.canSend(Priority::Sos, 200000, 0));
  b.record(200000, 0);                          // 0〜200ms に送信
  CHECK(!b.canSend(Priority::Sos, 100000, 250));// 休止 100ms 未満(終了200ms+100ms=300ms)
  CHECK(b.canSend(Priority::Sos, 100000, 300));
  b.record(300000, 300);                        // 累計 500ms
  b.record(300000, 700);                        // 累計 800ms
  CHECK(b.usedUs(800) == 800000);

  // 800ms 使用済み: REPORT(上限の80%=800ms)はもう送れない。SOS は 1000ms まで可
  CHECK(!b.canSend(Priority::Report, 100000, 1100));
  CHECK(!b.canSend(Priority::Beacon, 100000, 1100));
  CHECK(b.canSend(Priority::Sos, 100000, 1100));
  CHECK(!b.canSend(Priority::Sos, 300000, 1100));  // 上限 1000ms を超える
  CHECK(b.canSend(Priority::Instruct, 200000, 1100));

  // 窓が過ぎると記録が落ちる
  CHECK(b.usedUs(10000) == 600000);   // 0 の記録(200ms)が窓外
  CHECK(b.usedUs(20000) == 0);
  CHECK(b.canSend(Priority::Report, 100000, 20000));

  // 台帳が満杯のときは安全側(拒否)
  TxBudget<2> s(cfg);
  s.record(10000, 0);
  s.record(10000, 200);
  CHECK(!s.canSend(Priority::Sos, 10000, 400));
}

static void test_budget_defaults() {
  puts("tx budget defaults");
  // COMPLIANCE.md §3 の調査結果(128µs 方式: 1回 0.4 秒以下、1時間 360 秒以下、休止 2ms 以上)の内側であること
  BudgetConfig d;
  CHECK(d.singleMaxMs <= 200);          // 400ms の半分。ARIB 英訳の「200ms 超は休止 10 倍」も避ける
  CHECK(d.windowLimitMs <= 360000 / 10);
  CHECK(d.minGapMs >= 2);
  CHECK(d.windowMs == 3600000);
  // 17 バイトのフレームは SF7〜9 なら 1 回の上限に収まり、SF10 以上は既定では送れない
  TxBudget<8> b;
  CHECK(b.canSend(Priority::Report, loraAirtimeUs(17, 7), 0));
  CHECK(b.canSend(Priority::Report, loraAirtimeUs(17, 9), 0));
  CHECK(!b.canSend(Priority::Report, loraAirtimeUs(17, 10), 0));
  CHECK(!b.canSend(Priority::Sos, loraAirtimeUs(17, 10), 0));
}

// ---- 電波法ガード ----
static void test_tx_guard() {
  puts("tx guard");
#if RERAY_TX_ENABLE == 0
  static_assert(!txEnabled(), "TX must be disabled by default");
  CHECK(!txEnabled());
#endif
  CHECK(maxTxDbm() <= 12);
  CHECK(defaultTxDbm() <= maxTxDbm());
  CHECK(clampTxDbm(100).dbm == maxTxDbm() && clampTxDbm(100).clamped);
  CHECK(clampTxDbm(13).dbm <= 12);
  CHECK(clampTxDbm(maxTxDbm()).dbm == maxTxDbm() && !clampTxDbm(maxTxDbm()).clamped);
  CHECK(clampTxDbm(-5).dbm == 0 && clampTxDbm(-5).clamped);
}

void run_core_tests() {
  test_sha_hmac();
  test_frame_roundtrip();
  test_frame_layout();
  test_frame_security();
  test_frame_encode_limits();
  test_dedup();
  test_seq_tracker();
  test_airtime();
  test_budget();
  test_budget_defaults();
  test_tx_guard();
}
