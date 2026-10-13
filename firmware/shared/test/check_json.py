"""出口が作る JSON が、JSON として正しく、Drive 共通仕様「02_メッセージ形式」の必須項目を持つか確認する。"""
import json
import sys

COMMON = {"v", "type", "src", "seq", "ts"}
REQUIRED = {"signal": COMMON | {"code", "via"}, "ack": COMMON | {"ack_src", "ack_seq"}}

lines = [l for l in open(sys.argv[1], encoding="utf-8").read().splitlines() if l]
if not lines:
    sys.exit("NG: サンプルが空")
for l in lines:
    d = json.loads(l)
    need = REQUIRED[d["type"]]
    missing = need - d.keys()
    if missing:
        sys.exit(f"NG: {d['type']} に必須項目がない: {sorted(missing)}  {l}")
    if d["v"] != 1 or not isinstance(d["seq"], int) or not isinstance(d["ts"], int):
        sys.exit(f"NG: 型が違う: {l}")
    if d["type"] == "signal" and d["via"] != "lora":
        sys.exit(f"NG: via が lora でない: {l}")
print(f"OK: {len(lines)} 件の JSON が正しい")
