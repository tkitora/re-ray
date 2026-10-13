// T-Beam ⇄ XIAO の UART プロトコルのテスト。
#include <stdio.h>
#include <string.h>

#include "harness.h"
#include "reray/uart_proto.h"

using namespace reray;

// 改行を取り除いて解析する
static bool parseOne(const char* s, UartMsg& m) {
  char line[256];
  strncpy(line, s, sizeof line - 1);
  line[sizeof line - 1] = '\0';
  size_t n = strlen(line);
  if (n && line[n - 1] == '\n') line[n - 1] = '\0';
  return parseLine(line, m);
}

static void test_format_and_parse() {
  puts("uart format/parse");
  char b[200];
  UartMsg m;

  CHECK(formatState(12, LinkState::Server, b, sizeof b) > 0 && strcmp(b, "STATE,12,SERVER\n") == 0);
  CHECK(parseOne(b, m) && m.kind == UartKind::State && m.seq == 12 && m.state == LinkState::Server);
  const LinkState all[] = {LinkState::Sent, LinkState::Near, LinkState::Server, LinkState::Retry, LinkState::Fail};
  for (LinkState s : all) {
    formatState(65535, s, b, sizeof b);
    CHECK(parseOne(b, m) && m.state == s && m.seq == 65535);
  }

  CHECK(formatInstr(7, 5, 3, b, sizeof b) > 0 && strcmp(b, "INSTR,7,5,3\n") == 0);
  CHECK(parseOne(b, m) && m.kind == UartKind::Instr && m.seq == 7 && m.code == 5 && m.param == 3);

  CHECK(formatStatus(82, 3, 1, true, false, b, sizeof b) > 0 && strcmp(b, "STATUS,82,3,1,1,0\n") == 0);
  CHECK(parseOne(b, m) && m.kind == UartKind::Status && m.batt == 82 && m.zone == 3 && m.role == 1);
  CHECK(m.loraOk && !m.wifiOk);

  CHECK(formatLog("区域5へ物資搬送", b, sizeof b) > 0);
  CHECK(parseOne(b, m) && m.kind == UartKind::Log && strcmp(m.text, "区域5へ物資搬送") == 0);
  CHECK(strchr("LOG,a,b,c\n", ',') != nullptr && parseOne("LOG,a,b,c", m) && strcmp(m.text, "a,b,c") == 0);  // 本文にカンマ可

  CHECK(formatBtn(2, b, sizeof b) > 0 && strcmp(b, "BTN,2\n") == 0);
  CHECK(parseOne(b, m) && m.kind == UartKind::Btn && m.btnId == 2);
  CHECK(formatAckInstr(9, b, sizeof b) > 0 && strcmp(b, "ACK_INSTR,9\n") == 0);
  CHECK(parseOne(b, m) && m.kind == UartKind::AckInstr && m.seq == 9);

  CHECK(formatState(1, LinkState::Sent, b, 8) == 0);  // バッファ不足
}

static void test_parse_rejects() {
  puts("uart parse rejects");
  UartMsg m;
  m.btnId = 77;
  const char* bad[] = {
      "",          "FOO,1",        "STATE,1",        "STATE,1,BOGUS",    "STATE,70000,SENT", "STATE,-1,SENT",
      "STATE,1,SENT,x", "STATE,1,sent", "INSTR,1,2",   "INSTR,1,2,3,4",   "INSTR,1,256,0",    "INSTR,a,1,1",
      "STATUS,101,0,0,0,0", "STATUS,1,2,3,4",  "STATUS,1,2,3,2,0", "STATUS,1,2,3,1,0,9", "STATUS,1,2,3,1,",
      "BTN,",      "BTN,256",      "BTN,1x",         "BTN, 1",           "ACK_INSTR,",       "ACK_INSTR,65536",
      "ACK_INSTR,1,2", "state,1,SENT",
  };
  for (const char* s : bad) {
    CHECK(!parseLine(s, m));
    CHECK(m.btnId == 77);  // 失敗時は出力を触らない
  }
  // 長すぎる LOG は拒否
  char longLog[300] = "LOG,";
  memset(longLog + 4, 'a', 150);
  longLog[154] = '\0';
  CHECK(!parseLine(longLog, m));
}

static void test_log_sanitize() {
  puts("uart log sanitize");
  char b[256];
  UartMsg m;
  // 改行は空白に変える(1行1メッセージを壊さない)
  CHECK(formatLog("a\nb\r\nc", b, sizeof b) > 0);
  CHECK(strcmp(b, "LOG,a b  c\n") == 0);
  CHECK(strchr(b, '\n') == b + strlen(b) - 1);

  // 120バイトで切る。ASCII
  char ascii[200];
  memset(ascii, 'x', 150);
  ascii[150] = '\0';
  CHECK(formatLog(ascii, b, sizeof b) == 4 + kUartLogMax + 1);
  CHECK(parseOne(b, m) && strlen(m.text) == kUartLogMax);

  // 全角41文字(123バイト) → 40文字(120バイト)で、文字の途中では切らない
  char jp[200] = "";
  for (int i = 0; i < 41; i++) strcat(jp, "あ");
  CHECK(strlen(jp) == 123);
  CHECK(formatLog(jp, b, sizeof b) > 0);
  CHECK(parseOne(b, m) && strlen(m.text) == 120);
  CHECK(strncmp(m.text + 117, "あ", 3) == 0);

  // 119 バイトの ASCII の次が全角: 120 バイト目で文字が割れるので、手前(119)まで戻る
  char mix[200];
  memset(mix, 'a', 119);
  mix[119] = '\0';
  strcat(mix, "あ");
  CHECK(formatLog(mix, b, sizeof b) > 0);
  CHECK(parseOne(b, m) && strlen(m.text) == 119);
  for (size_t i = 0; i < strlen(m.text); i++) CHECK(m.text[i] == 'a');

  // 空の本文
  CHECK(formatLog("", b, sizeof b) > 0 && strcmp(b, "LOG,\n") == 0);
}

static void feed(LineReader& r, const char* s, int& lines, char (*got)[200], int maxLines) {
  const char* line;
  for (const char* p = s; *p; p++) {
    if (r.feed((uint8_t)*p, line) && lines < maxLines) {
      strncpy(got[lines], line, 199);
      got[lines][199] = '\0';
      lines++;
    }
  }
}

static void test_line_reader() {
  puts("uart line reader");
  LineReader r;
  char got[8][200];
  int n = 0;

  feed(r, "BTN,1\r\nACK_INSTR,5\n", n, got, 8);  // CRLF も可
  CHECK(n == 2 && strcmp(got[0], "BTN,1") == 0 && strcmp(got[1], "ACK_INSTR,5") == 0);

  n = 0;
  feed(r, "\n\r\n\nSTATE,1,SENT", n, got, 8);  // 空行は無視。改行が来るまで出さない
  CHECK(n == 0);
  feed(r, "\n", n, got, 8);
  CHECK(n == 1 && strcmp(got[0], "STATE,1,SENT") == 0);

  // 分割して届いても同じ
  n = 0;
  feed(r, "INS", n, got, 8);
  feed(r, "TR,1,2", n, got, 8);
  feed(r, ",3\nBT", n, got, 8);
  feed(r, "N,9\n", n, got, 8);
  CHECK(n == 2 && strcmp(got[0], "INSTR,1,2,3") == 0 && strcmp(got[1], "BTN,9") == 0);

  // 長すぎる行は捨てて、次の行から復帰する
  n = 0;
  char junk[400];
  memset(junk, 'z', 300);
  junk[300] = '\n';
  junk[301] = '\0';
  feed(r, junk, n, got, 8);
  CHECK(n == 0 && r.overflows() == 1);
  feed(r, "BTN,3\n", n, got, 8);
  CHECK(n == 1 && strcmp(got[0], "BTN,3") == 0);

  // 上限ちょうど(160)の行は通る
  n = 0;
  char edge[200];
  memset(edge, 'q', kUartLineMax);
  edge[kUartLineMax] = '\n';
  edge[kUartLineMax + 1] = '\0';
  feed(r, edge, n, got, 8);
  CHECK(n == 1 && strlen(got[0]) == kUartLineMax);
}

void run_uart_tests() {
  test_format_and_parse();
  test_parse_rejects();
  test_log_sanitize();
  test_line_reader();
}
