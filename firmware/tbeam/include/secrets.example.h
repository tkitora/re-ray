// 雛形。コピーして secrets.h を作り、値を入れる。secrets.h は git に入れない。
#pragma once

#define WIFI_SSID      "your-ssid"
#define WIFI_PASSWORD  "your-password"
#define MQTT_HOST      "192.168.0.10"   // PC の IP アドレス
#define MQTT_PORT      1883

// 開発用のダミー鍵(本番では別の鍵を使う)。16バイト。
#define RERAY_HMAC_KEY { 0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0A,0x0B,0x0C,0x0D,0x0E,0x0F }

// この端末の番号(u16)。端末ごとに変える。
#define RERAY_NODE_ID  1
