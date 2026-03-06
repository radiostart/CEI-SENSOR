"""PlatformIO custom upload: ESP32-C3 내장 USB JTAG (OpenOCD).

플래시 후 자동 리셋하여 새 펌웨어 즉시 실행.
다운로드 모드 진입: BOOT 누른 채 USB 연결 → BOOT 놓기
"""
Import("env")

import os
import subprocess
import sys


def upload_via_openocd(source, target, env):
    pkg = env.PioPlatform().get_package_dir("tool-openocd-esp32")
    openocd = os.path.join(pkg, "bin", "openocd")
    scripts = os.path.join(pkg, "share", "openocd", "scripts")
    build = env.subst("$BUILD_DIR")

    cmd = [
        openocd, "-s", scripts,
        "-f", "board/esp32c3-builtin.cfg",
        "-c", "program_esp {f} 0x0 verify".format(
            f=os.path.join(build, "bootloader.bin")),
        "-c", "program_esp {f} 0x8000 verify".format(
            f=os.path.join(build, "partitions.bin")),
        "-c", "program_esp {f} 0x10000 verify".format(
            f=os.path.join(build, "firmware.bin")),
        "-c", "reset run",
        "-c", "shutdown",
    ]

    print("\n=== OpenOCD JTAG Upload ===")
    result = subprocess.run(cmd)
    if result.returncode != 0:
        print("\nUpload failed! 다운로드 모드 + sudo 필요:")
        print("  BOOT 누른 채 USB 연결 → sudo pio run -t upload")
        sys.exit(result.returncode)


env.Replace(UPLOADCMD=upload_via_openocd)
