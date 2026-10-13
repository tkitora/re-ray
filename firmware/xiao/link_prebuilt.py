# SDK の link_prebuilt.py (sdk/guide01/esp32/examples/full_demo/link_prebuilt.py) を元にしたもの。
# SDK 本体は改変しない方針なので、ここにコピーして、libs の場所だけをこのリポジトリの配置に合わせた。
import os

Import("env")

ARCH_TARGET = {
    "esp32":   ("xtensa", "esp32"),
    "esp32s3": ("xtensa", "esp32s3"),
    "esp32c3": ("riscv",  "esp32c3"),
    "esp32c6": ("riscv",  "esp32c6"),
}

board = env.BoardConfig()
mcu = board.get("build.mcu", "esp32")

if mcu not in ARCH_TARGET:
    print(f"[guide01_protocol] unsupported MCU: {mcu}")
    Exit(1)

arch, target = ARCH_TARGET[mcu]
proj_root = env["PROJECT_DIR"]
# firmware/xiao -> ../../sdk/guide01/libs
libs_root = os.path.abspath(os.path.join(proj_root, "..", "..", "sdk", "guide01", "libs"))

lib_path = os.path.join(libs_root, "esp32", arch, target, "libguide01_protocol.a")
inc_dir  = os.path.join(libs_root, "include")

if not os.path.exists(lib_path):
    print(f"[guide01_protocol] prebuilt not found: {lib_path}")
    print("[guide01_protocol] sdk/README.md の手順で sdk/guide01 を clone してください")
    Exit(1)

print(f"[guide01_protocol] linking {lib_path}")
env.Append(CPPPATH=[inc_dir])
env.Append(LIBS=[File(lib_path)])
