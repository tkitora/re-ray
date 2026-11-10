// ライブラリ
// #include <M5Unified.h>
#include <NimBLEDevice.h>
#include <vector>
#include <string>
#include <cstring>
#include <cstddef>

// GUIDE01専用の機能を読み込み
extern "C" {
 #include <guide01_protocol.h>
}

// 設定値を定義
// Local UTC offset in seconds (JST = UTC+9). g01_time_set applies no time
// zone, so add it before passing the timestamp.
static const int32_t LOCAL_UTC_OFFSET_SEC = 9 * 3600;
static const uint32_t SCAN_TIMEOUT_SEC = 8;
static const uint32_t WRITE_GAP_MS     = 3000;
static const uint32_t TOUCH_LISTEN_MS  = 10000;
static const uint16_t REQUEST_MTU      = 247;

// 機器の情報を保存
static G01ScanFilter g_filter;
static G01TouchConsts g_touch;
static NimBLEAddress g_target_addr;
static std::string   g_target_name;
static bool          g_found = false;

// タッチ操作の名前判定
static const char* touch_event_name(uint8_t e) {
    if (e == g_touch.single_click)  return "single_click";
    if (e == g_touch.double_click)  return "double_click";
    if (e == g_touch.swipe_forward) return "swipe_forward";
    if (e == g_touch.swipe_back)    return "swipe_back";
    if (e == g_touch.long_press)    return "long_press";
    return "(unknown)";
}
// 検索した機器がGUIDE01かを判定
static bool match_filter(const NimBLEAdvertisedDevice* dev) {
    if (!dev->haveManufacturerData()) return false;
    std::string mfr = dev->getManufacturerData();
    if (mfr.size() < 2u + g_filter.data_size) return false;
    uint16_t cid = (uint8_t)mfr[0] | ((uint16_t)(uint8_t)mfr[1] << 8);
    if (cid != g_filter.id) return false;
    return memcmp(mfr.data() + 2, g_filter.data, g_filter.data_size) == 0;
}
// Bluetooth機器が見つかったときの処理
class ScanCb : public NimBLEScanCallbacks {
    void onResult(const NimBLEAdvertisedDevice* dev) override {
        if (g_found || !match_filter(dev)) return;

        std::string name = dev->getName();
        if (name.empty()) name = "(no name)";

        g_target_name = name;
        g_target_addr = dev->getAddress();
        g_found = true;

        Serial.printf("[scan] %s  %s  rssi=%d\n",
                      name.c_str(),
                      g_target_addr.toString().c_str(),
                      dev->getRSSI());

        NimBLEDevice::getScan()->stop();
    }
};
// Bluetoothの検索を開始
static bool do_scan() {
    g_filter = g01_get_scan_filter(); g_found = false;
    NimBLEDevice::init(""); NimBLEDevice::setMTU(REQUEST_MTU);
    NimBLEScan* scan = NimBLEDevice::getScan();
    scan->setScanCallbacks(new ScanCb(), false);
    scan->setActiveScan(true); scan->setInterval(100); scan->setWindow(99);
    Serial.printf("[scan] start (timeout %lus)...\n", (unsigned long)SCAN_TIMEOUT_SEC);
    scan->start(SCAN_TIMEOUT_SEC, false); scan->clearResults();
    return g_found;
}

// データを16進数表記
static std::string hex_dump(const uint8_t* data, size_t len, size_t cap = 16) {
    std::string s; size_t n = len < cap ? len : cap;
    for (size_t i = 0; i < n; i++) { char b[4]; snprintf(b, sizeof(b), "%02X ", data[i]); s += b; }
    if (len > cap) s += "...";
    return s;
}
// Bluetoothの通信先を探す
static NimBLERemoteCharacteristic* find_char(NimBLEClient* client, const char* uuid_str) {
    for (auto svc : client->getServices(false)) {
        for (auto chr : svc->getCharacteristics(false)) {
            if (chr->getUUID() == NimBLEUUID(uuid_str)) return chr;
        }
    }
    return nullptr;
}
static bool write_buf(NimBLEClient* client, const char* label, const char* uuid,const uint8_t* data, int n) {
    auto chr = find_char(client, uuid);
    if (!chr) { Serial.printf("[write] %s: <not found>\n", label); return false; }
    if (n <= 0) { Serial.printf("[write] %s: build error (%d)\n", label, n); return false; }
    bool resp = !chr->canWriteNoResponse();
    bool ok = chr->writeValue(data, (size_t)n, resp);
    Serial.printf("[write] %s: %s  %dB  %s\n",
                  label, ok ? "OK" : "FAIL", n, hex_dump(data, (size_t)n).c_str());
    return ok;
}

/* Call a caller-buffer API and forward the result to write_buf.
 *
 * The buffer lives on the stack, so pick the G01_BUF_* that fits: using
 * G01_BUF_MAX (4096) everywhere overflows the ESP32 loopTask stack (8 KB).
 * This helper only covers commands / weather / motion / lyrics. */

//  共通関数でデータを作って送信

#pragma once
#include "write_call.h"

// GUIDE01から通知を受信
static void on_cmd_notify(NimBLERemoteCharacteristic* , uint8_t* data, size_t len, bool ) {
    Serial.printf("[ble-notify/cmd] RX len=%zu raw=%s\n", len, hex_dump(data, len).c_str());
    if (len < 1) return;
    char version[64];
    if (g01_parse_resource_version_response(data, len, version, sizeof(version)) == 0) {
        Serial.printf("[resource_version] %s\n", version);
        return;
    }
    if (data[0] != g_touch.notify_type) {
        Serial.printf("[ble-notify/cmd] non-touch type=0x%02X (expected 0x%02X)\n",
                      data[0], g_touch.notify_type);
        return;
    }
    G01TouchEventData ev{};
    int rc = g01_parse_touch_event(data, len, &ev);
    if (rc == 0) {
        Serial.printf("[touch_event] page=0x%02X  event=%s\n",
                      ev.page_id, touch_event_name(ev.touch_event));
    } else {
        Serial.printf("[touch_event] parse error: rc=%d\n", rc);
    }
}
// FILEの通知を処理
static void on_file_notify(NimBLERemoteCharacteristic* , uint8_t* data, size_t len, bool ) {
    int rc = g01_parse_gif_text_upload_response(data, len);
    Serial.printf("[ble-notify/file] %s (upload_rc=%d)\n", hex_dump(data, len).c_str(), rc);
}
// 通知の受信を登録
static void subscribe_notifications(NimBLEClient* client) {
    g_touch = g01_get_touch_consts();
    Serial.printf("[ble-notify] touch consts: type=0x%02X  single=0x%02X double=0x%02X "
                  "fwd=0x%02X back=0x%02X long=0x%02X\n",
                  g_touch.notify_type, g_touch.single_click, g_touch.double_click,
                  g_touch.swipe_forward, g_touch.swipe_back, g_touch.long_press);

    auto cmd = find_char(client, g01_get_ble_cmd_char());
    if (!cmd) {
        Serial.println("[ble-notify] CMD char NOT FOUND");
    } else {
        Serial.printf("[ble-notify] CMD found: canNotify=%d canIndicate=%d\n",
                      cmd->canNotify(), cmd->canIndicate());
        bool ok = cmd->subscribe(true, on_cmd_notify, true);
        Serial.printf("[ble-notify] CMD subscribe %s\n", ok ? "OK" : "FAIL");
    }

    auto file = find_char(client, g01_get_ble_file_char());
    if (!file) {
        Serial.println("[ble-notify] FILE char NOT FOUND");
    } else {
        Serial.printf("[ble-notify] FILE found: canNotify=%d canIndicate=%d\n",
                      file->canNotify(), file->canIndicate());
        bool ok = file->subscribe(true, on_file_notify, true);
        Serial.printf("[ble-notify] FILE subscribe %s\n", ok ? "OK" : "FAIL");
    }
}
// 電池情報を読み取り
static void read_battery_like(NimBLEClient* client, const char* label, const char* uuid) {
    auto chr = find_char(client, uuid);
    if (!chr) { Serial.printf("[read] %s: <not found>\n", label); return; }
    std::string val = chr->readValue();
    G01BatteryData out{};
    int rc = g01_parse_battery_response((const uint8_t*)val.data(), val.size(), &out);
    if (rc == 0) Serial.printf("[read] %s: level=%d%%  raw=%s\n",
                                label, out.level, hex_dump((const uint8_t*)val.data(), val.size()).c_str());
    else         Serial.printf("[read] %s: parse error %d\n", label, rc);
}
// 文字列情報を読み取り
static void read_utf8(NimBLEClient* client, const char* label, const char* uuid) {
    auto chr = find_char(client, uuid);
    if (!chr) { Serial.printf("[read] %s: <not found>\n", label); return; }
    std::string val = chr->readValue();
    while (!val.empty() && val.back() == 0) val.pop_back();
    Serial.printf("[read] %s: \"%s\"\n", label, val.c_str());
}
// 読み取りを順番に実行
static void read_sequence(NimBLEClient* client) {
    Serial.println("--- read sequence ---");
    read_battery_like(client, "battery",     g01_get_ble_battery_level_char());
    read_battery_like(client, "box_battery", g01_get_ble_box_battery_char());
    read_utf8       (client, "serial",      g01_get_ble_serial_number_uuid());
    read_utf8       (client, "firmware",    g01_get_ble_firmware_revision_uuid());
    read_utf8       (client, "hardware",    g01_get_ble_hardware_revision_uuid());
    read_utf8       (client, "software",    g01_get_ble_software_revision_uuid());

    Serial.println("--- resource version (query + notify) ---");
    write_call(client, "resource_version_query", g01_get_ble_cmd_char(), g01_resource_version_query);
    delay(2000);
}

static void write_feature_demo(NimBLEClient* client) {
    Serial.println("--- write demo (3s gap) ---");

    // g01_time_set splits the epoch seconds it is given; it applies no time
    // zone. Note the ESP32 clock starts at 1970 until the RTC is synced.
    uint32_t ts = (uint32_t)time(nullptr) + LOCAL_UTC_OFFSET_SEC;
    write_call(client, "time_set",            g01_get_ble_settings_time_char(), g01_time_set, ts);
    delay(WRITE_GAP_MS);

    write_call(client, "brightness(50)",      g01_get_ble_brightness_char(), g01_brightness, 50);
    delay(WRITE_GAP_MS);

    write_call(client, "sleep_time(30s)",     g01_get_ble_sleep_time_char(), g01_sleep_time_set, 30);
    delay(WRITE_GAP_MS);

    write_call(client, "notify_display_time(10s)", g01_get_ble_cmd_char(), g01_notification_display_time, 10);
    delay(WRITE_GAP_MS);

    write_call(client, "weather(22C)",        g01_get_ble_weather_char(), g01_weather, "22C");
    delay(WRITE_GAP_MS);

    write_call(client, "lyric: song_info",    g01_get_ble_lyric_char(), g01_song_info, "Demo Song", "ESP32 Player");
    delay(WRITE_GAP_MS);
    write_call(client, "lyric: data",         g01_get_ble_lyric_char(), g01_lyric_data, "line 1: hello from esp32", "line 2: lyric demo", "line 3: bye");
    delay(WRITE_GAP_MS);
    write_call(client, "lyric: stop",         g01_get_ble_lyric_char(), g01_lyric_stop);
    delay(WRITE_GAP_MS);

    write_call(client, "page_switch_to_time", g01_get_ble_cmd_char(), g01_page_switch_to_time);
    delay(WRITE_GAP_MS);

    write_call(client, "gif_text status_query_all_gifs", g01_get_ble_file_char(), g01_gif_text_status_query_all_gifs);
    delay(WRITE_GAP_MS);

    const G01MotionCmdConsts mc = g01_get_motion_cmd_consts();
    const G01MotionTypeConsts mt = g01_get_motion_type_consts();
    write_call(client, "motion: layout(3)",   g01_get_ble_motion_char(), g01_motion_set_layout, mc.layout_three);
    delay(WRITE_GAP_MS);

    G01MotionItem motion_items[2];
    memset(motion_items, 0, sizeof(motion_items));
    motion_items[0].type = mt.speed;      motion_items[0].value = 33;
    motion_items[1].type = mt.heart_rate; motion_items[1].value = 105;
    write_call(client, "motion: data(speed+hr)", g01_get_ble_motion_char(), g01_motion_data, motion_items, 2);
    delay(WRITE_GAP_MS);

    write_call(client, "motion: layout(1)", g01_get_ble_motion_char(), g01_motion_set_layout, mc.layout_one);
    delay(WRITE_GAP_MS);
    G01MotionItem motion_one[1];
    memset(motion_one, 0, sizeof(motion_one));
    motion_one[0].type = mt.heart_rate; motion_one[0].value = 120;
    write_call(client, "motion: data(1 elem)", g01_get_ble_motion_char(), g01_motion_data, motion_one, 1);
    delay(WRITE_GAP_MS);

    write_call(client, "motion: layout(5)", g01_get_ble_motion_char(), g01_motion_set_layout, mc.layout_five);
    delay(WRITE_GAP_MS);
    G01MotionItem motion_five[5];
    memset(motion_five, 0, sizeof(motion_five));
    motion_five[0].type = mt.speed;      motion_five[0].value = 33;
    motion_five[1].type = mt.heart_rate; motion_five[1].value = 105;
    motion_five[2].type = mt.distance;   motion_five[2].value = 5000;
    motion_five[3].type = mt.power;      motion_five[3].value = 240;
    motion_five[4].type = mt.cadence;    motion_five[4].value = 90;
    write_call(client, "motion: data(5 elem)", g01_get_ble_motion_char(), g01_motion_data, motion_five, 5);
    delay(WRITE_GAP_MS);
}
#if 0
static void image_transfer(NimBLEClient* client) {
    Serial.println("--- image transfer ---");
    auto chr = find_char(client, g01_get_ble_file_char());
    if (!chr) { Serial.println("[image] FILE characteristic not found"); return; }

    size_t mtu = client->getMTU(); if (mtu < 23) mtu = 23;
    Serial.printf("[image] source: %zu bytes, mtu=%zu\n", TEST_IMAGE_LEN, mtu);

    alignas(max_align_t) uint8_t mem[G01_IMAGE_BUILDER_SIZE_MAX];
    G01ImageBuilder* b = (G01ImageBuilder*)mem;
    int rc = g01_image_builder_init(b, TEST_IMAGE_DATA, TEST_IMAGE_LEN, mtu);
    if (rc != 0) { Serial.printf("[image] builder_init rc=%d\n", rc); return; }
    const size_t chunk_count = g01_image_builder_chunk_count(b);
    const size_t total = 1 + chunk_count;
    Serial.printf("[image] chunked into %zu message(s)\n", total);

    bool resp = !chr->canWriteNoResponse();

    uint8_t info[G01_BUF_CHUNK];
    int info_n = g01_image_builder_info(b, info, sizeof(info));
    bool ok = chr->writeValue(info, info_n, resp);
    Serial.printf("[image] chunk 1/%zu (%zuB) %s\n", total, info_n, ok ? "OK" : "FAIL");
    if (total > 1) delay(20);

    for (size_t i = 0; i < chunk_count; i++) {
        uint8_t c[G01_BUF_CHUNK];
        int c_n = g01_image_builder_chunk(b, i, c, sizeof(c));
        ok = chr->writeValue(c, c_n, resp);
        if (!ok || i + 1 == chunk_count || (i % 10) == 0) {
            Serial.printf("[image] chunk %zu/%zu (%zuB) %s\n",
                          i + 2, total, c_n, ok ? "OK" : "FAIL");
        }
        if (i + 1 < chunk_count) delay(20);
    }
    Serial.println("[image] all chunks sent");
}
#endif

static const uint8_t GIF_TEXT_IMAGE_ID = 0;

#if 0
static void gif_text_demo(NimBLEClient* client) {
    Serial.println("--- gif_text sample (upload + query + display) ---");
    auto file_chr = find_char(client, g01_get_ble_file_char());
    auto disp_chr = find_char(client, g01_get_ble_gif_text_display_char());
    if (!file_chr || !disp_chr) {
        Serial.println("[gif_text] FILE or GIF_TEXT_DISPLAY characteristic not found");
        return;
    }

    size_t mtu = client->getMTU(); if (mtu < 23) mtu = 23;
    alignas(max_align_t) uint8_t mem[G01_GIF_TEXT_BUILDER_SIZE_MAX];
    G01GifTextBuilder* b = (G01GifTextBuilder*)mem;
    int rc = g01_gif_text_builder_init_image(
        b, GIF_TEXT_IMAGE_ID, TEST_IMAGE_DATA, TEST_IMAGE_LEN, mtu);
    if (rc != 0) { Serial.printf("[gif_text] builder_init rc=%d\n", rc); return; }
    const size_t chunk_count = g01_gif_text_builder_chunk_count(b);
    const size_t total = 1 + chunk_count;
    Serial.printf("[gif_text] image (%zu bytes) chunked into %zu message(s)\n",
                  TEST_IMAGE_LEN, total);

    bool file_resp = !file_chr->canWriteNoResponse();

    uint8_t start[G01_BUF_CHUNK];
    int start_n = g01_gif_text_builder_start(b, start, sizeof(start));
    bool ok = file_chr->writeValue(start, start_n, file_resp);
    Serial.printf("[gif_text] upload 1/%zu %s\n", total, ok ? "OK" : "FAIL");
    if (total > 1) delay(20);

    for (size_t i = 0; i < chunk_count; i++) {
        uint8_t c[G01_BUF_CHUNK];
        int c_n = g01_gif_text_builder_chunk(b, i, c, sizeof(c));
        ok = file_chr->writeValue(c, c_n, file_resp);
        if (!ok || i + 1 == chunk_count || (i % 10) == 0) {
            Serial.printf("[gif_text] upload %zu/%zu %s\n",
                          i + 2, total, ok ? "OK" : "FAIL");
        }
        if (i + 1 < chunk_count) delay(20);
    }
    delay(500);

    Serial.println("[gif_text] query images");
    uint8_t query[G01_BUF_COMMAND];
    int query_n = g01_gif_text_status_query_images(query, sizeof(query));
    file_chr->writeValue(query, query_n, file_resp);
    delay(500);

    G01GifTextConsts gtc = g01_get_gif_text_consts();
    G01DisplayItem items[4];
    memset(items, 0, sizeof(items));
    items[0].layer_id = 0; items[0].type = gtc.element_type_image;
    items[0].x = gtc.pos_center; items[0].y = gtc.pos_center;
    items[0].resource_id = GIF_TEXT_IMAGE_ID;

    items[1].layer_id = 1; items[1].type = gtc.element_type_text;
    items[1].x =  50; items[1].y =  60; items[1].font_size = 64;
    items[1].color = (G01RgbColor){ 255, 0, 0 }; items[1].text = "Hello";

    items[2].layer_id = 2; items[2].type = gtc.element_type_text;
    items[2].x = 220; items[2].y = 170; items[2].font_size = 48;
    items[2].color = (G01RgbColor){ 0, 255, 0 }; items[2].text = "Hello";

    items[3].layer_id = 3; items[3].type = gtc.element_type_text;
    items[3].x = 390; items[3].y = 280; items[3].font_size = 32;
    items[3].color = (G01RgbColor){ 0, 0, 255 }; items[3].text = "Hello";

    /* Display elements need up to 4096 B, too much for the loopTask stack.
     * static is safe here: only one task calls this. */
    static uint8_t pkt[G01_BUF_GIF_TEXT];
    int pkt_n = g01_gif_text_display_elements(1, items, 4, pkt, sizeof(pkt));
    bool disp_resp = !disp_chr->canWriteNoResponse();
    ok = disp_chr->writeValue(pkt, pkt_n, disp_resp);
    Serial.printf("[gif_text] display elements %s (%zuB, image + 3 texts)\n",
                  ok ? "sent" : "FAIL", pkt_n);
}
#endif

static void notification_demo(NimBLEClient* client) {
    Serial.println("--- notification (single write) ---");
    auto chr = find_char(client, g01_get_ble_msg_notify_char());
    if (!chr) { Serial.println("[notification] MSG_NOTIFY char not found"); return; }
    bool resp = !chr->canWriteNoResponse();
    char tstr[20];
    time_t t = time(nullptr);
    struct tm lt;
    localtime_r(&t, &lt);
    strftime(tstr, sizeof(tstr), "%Y%m%dT%H%M%S", &lt);
    uint8_t pkt[G01_BUF_TEXT];
    int pkt_n = g01_notification("TestApp", "通知", "Hello from GUIDE01 SDK!", tstr, pkt, sizeof(pkt));
    bool ok = chr->writeValue(pkt, pkt_n, resp);
    Serial.printf("[notification] %s (%zuB)\n", ok ? "sent" : "FAILED", pkt_n);
}

static void touch_event_capture(NimBLEClient* client) {
    (void)client;
    Serial.printf("--- touch event capture (%lus) ---\n",
                  (unsigned long)(TOUCH_LISTEN_MS / 1000));
    Serial.println("[touch_event] listening... (touch the device)");
    delay(TOUCH_LISTEN_MS);
    Serial.println("[touch_event] done");
}

static void screen_mode_demo(NimBLEClient* client) {
    Serial.println("--- screen_mode (device may reboot) ---");
    write_call(client, "screen_mode left",  g01_get_ble_cmd_char(), g01_screen_mode_left);
    delay(WRITE_GAP_MS);
    write_call(client, "screen_mode right", g01_get_ble_cmd_char(), g01_screen_mode_right);
    Serial.println("[screen_mode] done (device likely rebooted, demo ends)");
}

void setup() {
    // auto cfg = M5.config(); M5.begin(cfg);
    Serial.begin(115200); delay(200);
    mqttSetup();
    Serial.println("ESP32 Start");

    Serial.println("MQTT Setup Start");
    mqttSetup();
    Serial.println("MQTT Setup Finish");
    Serial.printf("[boot] guide01_protocol version : %s\n", g01_get_version());

    if (!do_scan()) { Serial.println("[scan] no GUIDE device found"); return; }

    Serial.printf("[connect] %s (%s)\n",
                  g_target_name.c_str(), g_target_addr.toString().c_str());
    NimBLEClient* client = NimBLEDevice::createClient();
    client->setConnectionParams(12, 12, 0, 400);
    client->setConnectTimeout(10);
    if (!client->connect(g_target_addr)) { Serial.println("[connect] failed"); return; }
    Serial.printf("[connect] OK  mtu=%d\n", client->getMTU());

    Serial.println("[discover] start ...");
    if (!client->discoverAttributes()) {
        Serial.println("[discover] FAILED");
        return;
    }
    Serial.println("[discover] complete");

    subscribe_notifications(client);
    read_sequence(client);
    // write_feature_demo(client);
    // image_transfer(client);
    // gif_text_demo(client);
    notification_demo(client);
    touch_event_capture(client);
    screen_mode_demo(client);
//   Serial.begin(115200);
//   delay(1000);

//   Serial.println("M5StampS3 start");
//   Serial.println("GUIDE01 BLE image test");
}

void loop() {
    // M5.update();
    // mqttSetup();
    mqttLoop();
    delay(10);
}