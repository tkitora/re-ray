# firmware/tbeam/src/

Claude Code が実装する。まず LILYGO 公式サンプル(LoRa、GPS、PMU)を動かし、その上に作る。
`main.cpp`(役割の切り替え)、LoRa ドライバのラッパー、リレー処理、UART ブリッジ、MQTT ブリッジ(出口のみ)などに分ける。
