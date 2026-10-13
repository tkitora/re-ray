# sdk/

GUIDE01 SDK をここに配置する。

## 取得元

https://git.happylifecreators.com/guide-series-sdk/guide01

(リポジトリは Apache License 2.0。SDK の README には、`libs/` のビルド済みバイナリも同じ Apache-2.0 の範囲と書かれている(2026-10-13 に確認)。ただし `sdk/guide01/` は `.gitignore` で git 管理外にしてあるので、このリポジトリには含めない。)

## 配置

```bash
cd p-02/sdk
git clone https://git.happylifecreators.com/guide-series-sdk/guide01.git
```

## 確認すること

- `guide01/esp32/examples/` に `hello`、`scan_connect_battery`、`full_demo` がある
- `guide01/libs/esp32/<arch>/<target>/libguide01_protocol.a` に、**`esp32s3` 向け**がある(XIAO ESP32S3 用)
- SDK の README では、BLE のサンプルは Arduino + M5Unified + NimBLE 構成で、動作確認機は M5Stick-C とされている。**M5Unified を使わない構成(XIAO)に直す必要がある**ので、`full_demo` の BLE 部分だけを取り出して使った(`firmware/xiao/src/main.cpp`)

## 注意

- `sdk/` 以下は改変しない。改変が必要なコードは `firmware/` にコピーして行う。
- SDK 本体はこのリポジトリに入れない(`.gitignore` 済み)。必要な人は上の手順で clone する。
