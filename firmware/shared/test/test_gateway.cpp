// 端末ID、出口(LoRa→MQTT JSON、キュー)、報告の状態遷移、リレーの再送予約のテスト。
#include <stdio.h>
#include <string.h>

#include <initializer_list>

#include "harness.h"
#include "reray/frame.h"
#include "reray/gateway.h"
#include "reray/node_id.h"
#include "reray/relay_scheduler.h"
#include "reray/report_tracker.h"

using namespace reray;

static const uint8_t KEY[16] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};

static void test_node_id() {
  puts("node id");
  char b[16];
  CHECK(formatDeviceId(1, b, sizeof b) == 7 && strcmp(b, "dev-001") == 0);
  CHECK(formatDeviceId(42, b, sizeof b) == 7 && strcmp(b, "dev-042") == 0);
  CHECK(formatDeviceId(1000, b, sizeof b) == 8 && strcmp(b, "dev-1000") == 0);
  CHECK(formatDeviceId(0, b, sizeof b) == 2 && strcmp(b, "pc") == 0);
  CHECK(formatDeviceId(65535, b, sizeof b) == 3 && strcmp(b, "all") == 0);
  CHECK(formatDeviceId(1, b, 7) == 0);  // 終端が入らない

  uint16_t id = 99;
  CHECK(parseDeviceId("dev-001", id) && id == 1);
  CHECK(parseDeviceId("dev-1", id) && id == 1);
  CHECK(parseDeviceId("dev-65534", id) && id == 65534);
  CHECK(parseDeviceId("pc", id) && id == 0);
  CHECK(parseDeviceId("all", id) && id == 0xFFFF);
  const char* bad[] = {"", "dev-", "dev-0", "dev-000", "dev-65535", "dev-65536", "dev-1a", "dev--1", "devs-1", "DEV-001", "pc "};
  for (const char* s : bad) {
    id = 7;
    CHECK(!parseDeviceId(s, id));
    CHECK(id == 7);  // 失敗時は触らない
  }
  for (uint32_t v = 1; v < 65535; v += 977) {  // 往復
    formatDeviceId((uint16_t)v, b, sizeof b);
    CHECK(parseDeviceId(b, id) && id == v);
  }
}

static Frame report(uint16_t src, uint16_t seq, uint8_t code) {
  Frame f;
  f.type = Type::Report;
  f.ttl = 3;
  f.src = src;
  f.seq = seq;
  f.zone = 3;
  f.people = 2;
  f.injured = 1;
  f.rescue = 0;
  f.needs = 5;
  f.flags = withSignalCode(kFlagGpsValid, code);
  f.time = 1791849600;
  return f;
}

static void test_signal_json() {
  puts("signal json");
  char out[256];
  RxInfo rx;
  rx.rssi = -97;
  rx.snr = 5.5f;

  Frame f = report(1, 42, 2);
  size_t n = signalJson(f, rx, 111, false, out, sizeof out);
  CHECK(n > 0 && strlen(out) == n);
  CHECK(strcmp(out, "{\"v\":1,\"type\":\"signal\",\"src\":\"dev-001\",\"seq\":42,\"ts\":1791849600,\"code\":2,\"via\":\"lora\"}") == 0);

  // 追加項目は "x" の中にまとめる(共通仕様の項目は崩さない)
  n = signalJson(f, rx, 111, true, out, sizeof out);
  CHECK(n > 0 && strlen(out) == n);
  CHECK(strstr(out, "\"code\":2,\"via\":\"lora\",\"x\":{\"zone\":3,") != nullptr);
  CHECK(strstr(out, "\"rssi\":-97,\"snr\":5.5}}") != nullptr);
  CHECK(strstr(out, "\"gps\":true") != nullptr);

  // 参考: JSON として正しいかは `make json-check` で Python が確認する
  FILE* fp = fopen("build/sample_json.txt", "w");
  if (fp) {
    fprintf(fp, "%s\n", out);
    signalJson(f, rx, 111, false, out, sizeof out);
    fprintf(fp, "%s\n", out);
    f.type = Type::Sos;
    signalJson(f, rx, 111, true, out, sizeof out);
    fprintf(fp, "%s\n", out);
    Frame a;
    a.type = Type::Ack;
    a.src = 2;
    a.seq = 44;
    a.refSrc = 0;
    a.refSeq = 7;
    a.ackKind = (uint8_t)AckKind::InstructReceived;
    ackJson(a, 1791849601, out, sizeof out);
    fprintf(fp, "%s\n", out);
    fclose(fp);
  }

  // SOS は code=1、時刻が未同期(0)なら fallback
  f = report(300, 7, 4);
  f.type = Type::Sos;
  f.time = 0;
  n = signalJson(f, rx, 1791849999, false, out, sizeof out);
  CHECK(n > 0 && strstr(out, "\"src\":\"dev-300\"") && strstr(out, "\"ts\":1791849999") && strstr(out, "\"code\":1,"));

  // 定型信号コードが無い REPORT は code=0
  f = report(1, 1, 0);
  signalJson(f, rx, 1, false, out, sizeof out);
  CHECK(strstr(out, "\"code\":0,") != nullptr);

  // 不正入力
  CHECK(signalJson(report(1, 42, 2), rx, 1, true, out, 40) == 0);   // 小さすぎる
  CHECK(signalJson(report(1, 42, 2), rx, 1, false, out, 40) == 0);
  Frame b;
  b.type = Type::Beacon;
  CHECK(signalJson(b, rx, 1, false, out, sizeof out) == 0);          // 対象外の種別
  CHECK(signalJson(report(0, 1, 1), rx, 1, false, out, sizeof out) == 0 ||
        strstr(out, "\"src\":\"pc\"") != nullptr);                  // src=0 は端末として不正だが、落ちない
}

static void test_ack_json_and_topics() {
  puts("ack json / topic / server ack");
  char out[200];
  Frame a;
  a.type = Type::Ack;
  a.src = 2;
  a.seq = 44;
  a.refSrc = 0;
  a.refSeq = 7;
  a.ackKind = (uint8_t)AckKind::InstructReceived;
  size_t n = ackJson(a, 1791849601, out, sizeof out);
  CHECK(n > 0);
  CHECK(strcmp(out, "{\"v\":1,\"type\":\"ack\",\"src\":\"dev-002\",\"seq\":44,\"ts\":1791849601,\"ack_src\":\"pc\",\"ack_seq\":7}") == 0);
  a.ackKind = (uint8_t)AckKind::Near;
  CHECK(ackJson(a, 1, out, sizeof out) == 0);  // LoRa 内部の ACK は MQTT に出さない
  a.ackKind = (uint8_t)AckKind::Server;
  CHECK(ackJson(a, 1, out, sizeof out) == 0);

  char t[64];
  CHECK(upTopic(1, "signal", t, sizeof t) > 0 && strcmp(t, "reray/v1/up/dev-001/signal") == 0);
  CHECK(upTopic(12, "status", t, sizeof t) > 0 && strcmp(t, "reray/v1/up/dev-012/status") == 0);
  CHECK(upTopic(1, "signal", t, 10) == 0);

  // サーバー到達 ACK: 参照先が合っていて、署名つきで往復できる
  Frame ref = report(5, 0xFFFF, 3);
  Frame ack = makeServerAck(100, 9, 3, ref);
  CHECK(ack.type == Type::Ack && ack.src == 100 && ack.seq == 9 && ack.ttl == 3);
  CHECK(ack.refSrc == 5 && ack.refSeq == 0xFFFF && ack.ackKind == 2);
  uint8_t buf[kMaxFrameLen];
  Frame o;
  const size_t len = encodeFrame(ack, KEY, sizeof KEY, buf);
  CHECK(len == 14 && decodeFrame(buf, len, KEY, sizeof KEY, o) == DecodeStatus::Ok);
  CHECK(o.refSrc == 5 && o.refSeq == 0xFFFF && o.ackKind == (uint8_t)AckKind::Server);
}

static size_t enc(const Frame& f, uint8_t* buf) { return encodeFrame(f, KEY, sizeof KEY, buf); }

static void test_gateway_queue() {
  puts("gateway queue");
  GatewayQueue<3> q;
  uint8_t b[kMaxFrameLen];
  RxInfo rx;
  CHECK(q.empty());
  Frame f1 = report(1, 1, 2), f2 = report(1, 2, 2), f3 = report(1, 3, 2), f4 = report(1, 4, 2);
  CHECK(q.push(b, enc(f1, b), rx));
  CHECK(q.push(b, enc(f2, b), rx));
  CHECK(q.push(b, enc(f3, b), rx));
  CHECK(q.size() == 3 && q.dropped() == 0);
  Frame o;
  CHECK(decodeFrame(q.front().bytes, q.front().len, KEY, sizeof KEY, o) == DecodeStatus::Ok && o.seq == 1);  // FIFO
  q.pop();
  CHECK(q.size() == 2);

  // 満杯のとき、SOS 以外の一番古いものを捨てる
  GatewayQueue<3> s;
  Frame sos = report(2, 10, 1);
  sos.type = Type::Sos;
  s.push(b, enc(sos, b), rx);  // 先頭が SOS
  s.push(b, enc(f1, b), rx);
  s.push(b, enc(f2, b), rx);
  s.push(b, enc(f4, b), rx);   // f1 が捨てられる(SOS は先頭だが残る)
  CHECK(s.size() == 3 && s.dropped() == 1);
  decodeFrame(s.front().bytes, s.front().len, KEY, sizeof KEY, o);
  CHECK(o.type == Type::Sos && o.seq == 10);
  s.pop();
  decodeFrame(s.front().bytes, s.front().len, KEY, sizeof KEY, o);
  CHECK(o.seq == 2);

  // 全部 SOS なら一番古い SOS を捨てる
  GatewayQueue<2> a;
  Frame s1 = sos, s2 = sos, s3 = sos;
  s1.seq = 1; s2.seq = 2; s3.seq = 3;
  a.push(b, enc(s1, b), rx);
  a.push(b, enc(s2, b), rx);
  a.push(b, enc(s3, b), rx);
  CHECK(a.size() == 2 && a.dropped() == 1);
  decodeFrame(a.front().bytes, a.front().len, KEY, sizeof KEY, o);
  CHECK(o.seq == 2);

  CHECK(!q.push(b, 0, rx));
  CHECK(!q.push(b, kMaxFrameLen + 1, rx));
}

static void test_report_tracker() {
  puts("report tracker");
  ReportTracker<4> t;
  ReportState st;
  t.start(10, 0);
  CHECK(t.state(10, st) && st == ReportState::Sent);
  CHECK(t.poll(4999).kind == TrackerEvent::None);

  TrackerEvent e = t.poll(5000);  // 5秒後に1回目の再送
  CHECK(e.kind == TrackerEvent::Resend && e.seq == 10);
  CHECK(t.state(10, st) && st == ReportState::Retry);
  CHECK(t.poll(5000).kind == TrackerEvent::None);   // 同じ時刻で二重に出さない
  CHECK(t.poll(14999).kind == TrackerEvent::None);
  CHECK(t.poll(15000).kind == TrackerEvent::Resend);  // +10秒
  CHECK(t.poll(34999).kind == TrackerEvent::None);
  CHECK(t.poll(35000).kind == TrackerEvent::Resend);  // +20秒(3回目)
  CHECK(t.retries(10) == 3);
  CHECK(t.poll(54999).kind == TrackerEvent::None);
  e = t.poll(55000);                                  // 最後の待ちを過ぎたら諦める
  CHECK(e.kind == TrackerEvent::GiveUp && e.seq == 10);
  CHECK(t.state(10, st) && st == ReportState::Failed);
  CHECK(t.poll(999999).kind == TrackerEvent::None);   // 止まる(送り続けない)

  // 近傍の ACK で再送を止める → サーバー到達で完了。Near は Server を戻さない
  t.start(11, 100000);
  CHECK(t.poll(105000).kind == TrackerEvent::Resend);
  CHECK(t.onNear(11));
  CHECK(t.state(11, st) && st == ReportState::Near);
  CHECK(t.poll(200000).kind == TrackerEvent::None);
  CHECK(t.onServer(11) && t.state(11, st) && st == ReportState::Server);
  CHECK(!t.onNear(11) && t.state(11, st) && st == ReportState::Server);
  CHECK(!t.onServer(11));
  CHECK(!t.onNear(999) && !t.onServer(999));           // 知らない seq は無視

  // 待ちの途中でサーバー到達 → 再送しない
  t.start(12, 300000);
  CHECK(t.onServer(12));
  CHECK(t.poll(400000).kind == TrackerEvent::None);

  // seq の回り込み(65535 の次の 0)も別の報告として扱える
  t.start(65535, 500000);
  t.start(0, 500001);
  CHECK(t.onServer(0) && t.state(65535, st) && st == ReportState::Sent);

  // 枠が足りないとき、完了済みの古いものを先に捨てて、未完了は残す
  ReportTracker<2> small;
  small.start(1, 0);
  small.start(2, 0);
  small.onServer(1);
  small.start(3, 0);  // seq 1(完了済み)が追い出される
  CHECK(!small.state(1, st));
  CHECK(small.state(2, st) && st == ReportState::Sent);
  CHECK(small.state(3, st) && st == ReportState::Sent);
}

static void test_relay_scheduler() {
  puts("relay scheduler");
  RelayConfig cfg;
  uint8_t buf[kMaxFrameLen];
  Frame o;

  // ttl が 1 減り、署名は有効なまま。ttl=0 は再送しない
  {
    RelayScheduler<4> r(cfg);
    Frame f = report(1, 5, 2);
    f.ttl = 3;
    size_t len = enc(f, buf);
    CHECK(r.schedule(buf, len, f, 0, 0));
    RelayScheduler<4>::Job j;
    CHECK(!r.pop(cfg.range[3].minMs - 1, j));     // まだ時間が来ていない
    CHECK(r.pop(cfg.range[3].minMs, j));          // rand=0 なら最小値
    CHECK(decodeFrame(j.bytes, j.len, KEY, sizeof KEY, o) == DecodeStatus::Ok && o.ttl == 2 && o.seq == 5);
    f.ttl = 0;
    len = enc(f, buf);
    CHECK(!r.schedule(buf, len, f, 0, 0));
  }

  // バックオフは優先度ごとの範囲に収まる(乱数の両端)
  {
    const Type types[5] = {Type::Sos, Type::Instruct, Type::Ack, Type::Report, Type::Beacon};
    for (int p = 0; p < 5; p++) {
      for (uint32_t rnd : {0u, 1u, 12345u, 0xFFFFFFFFu}) {
        RelayScheduler<2> r(cfg);
        Frame f = report(1, 1, 2);
        f.type = types[p];
        f.ttl = 2;
        if (types[p] == Type::Ack) f.ackKind = 1;
        size_t len = enc(f, buf);
        CHECK(r.schedule(buf, len, f, 1000, rnd));
        RelayScheduler<2>::Job j;
        CHECK(!r.pop(1000 + cfg.range[p].minMs - 1, j));
        CHECK(r.pop(1000 + cfg.range[p].maxMs - 1, j));  // 必ず範囲内で出る
      }
    }
  }

  // 優先度: 同時刻なら SOS が先。SOS は REPORT より範囲が短い
  {
    RelayScheduler<4> r(cfg);
    Frame rep = report(1, 1, 2), sos = report(2, 1, 1);
    sos.type = Type::Sos;
    rep.ttl = sos.ttl = 2;
    r.schedule(buf, enc(rep, buf), rep, 0, 0);
    r.schedule(buf, enc(sos, buf), sos, 0, 0);
    RelayScheduler<4>::Job j;
    CHECK(r.pop(10000, j));
    decodeFrame(j.bytes, j.len, KEY, sizeof KEY, o);
    CHECK(o.type == Type::Sos);
    CHECK(r.pop(10000, j));
    decodeFrame(j.bytes, j.len, KEY, sizeof KEY, o);
    CHECK(o.type == Type::Report);
    CHECK(!r.pop(10000, j));
  }

  // 再送の抑制: 待っている間に k=2 回聞いたら取り消す。別のフレームには影響しない
  {
    RelayScheduler<4> r(cfg);
    Frame f = report(1, 7, 2), g = report(1, 8, 2);
    f.ttl = g.ttl = 2;
    r.schedule(buf, enc(f, buf), f, 0, 0);
    r.schedule(buf, enc(g, buf), g, 0, 0);
    CHECK(!r.onHeardAgain(1, 7, (uint8_t)Type::Report));  // 1回目
    CHECK(r.pending() == 2);
    CHECK(r.onHeardAgain(1, 7, (uint8_t)Type::Report));   // 2回目で取り消し
    CHECK(r.pending() == 1);
    CHECK(!r.onHeardAgain(1, 7, (uint8_t)Type::Report));  // もう無い
    CHECK(!r.onHeardAgain(1, 8, (uint8_t)Type::Sos));     // type が違えば別物
    RelayScheduler<4>::Job j;
    CHECK(r.pop(10000, j));
    decodeFrame(j.bytes, j.len, KEY, sizeof KEY, o);
    CHECK(o.seq == 8);
  }

  // 満杯: 高優先は低優先を押しのける。低優先は入れない
  {
    RelayScheduler<2> r(cfg);
    Frame a = report(1, 1, 2), b = report(1, 2, 2);
    a.type = Type::Beacon;
    b.type = Type::Report;
    a.ttl = b.ttl = 2;
    CHECK(r.schedule(buf, enc(a, buf), a, 0, 0));
    CHECK(r.schedule(buf, enc(b, buf), b, 0, 0));
    Frame sos = report(3, 1, 1);
    sos.type = Type::Sos;
    sos.ttl = 2;
    CHECK(r.schedule(buf, enc(sos, buf), sos, 0, 0));  // Beacon を押しのける
    Frame bc = a;
    bc.seq = 9;
    CHECK(!r.schedule(buf, enc(bc, buf), bc, 0, 0));   // 新しい Beacon は入れない
    CHECK(r.pending() == 2);
    RelayScheduler<2>::Job j;
    int sawBeacon = 0;
    while (r.pop(10000, j)) {
      decodeFrame(j.bytes, j.len, KEY, sizeof KEY, o);
      if (o.type == Type::Beacon) sawBeacon++;
    }
    CHECK(sawBeacon == 0);
  }
}

void run_gateway_tests() {
  test_node_id();
  test_signal_json();
  test_ack_json_and_topics();
  test_gateway_queue();
  test_report_tracker();
  test_relay_scheduler();
}
