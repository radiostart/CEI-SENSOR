"""
PlatformIO pre-build script: Git 태그에서 펌웨어 버전 자동 추출

Git 태그 형식: v1.2.3
빌드 시 -D 매크로로 APP_FW_MAJOR/MINOR/PATCH 주입
태그 없으면 0.0.0 폴백
"""
import subprocess
import re

Import("env")

def get_git_version():
    try:
        out = subprocess.check_output(
            ["git", "describe", "--tags", "--abbrev=0"],
            stderr=subprocess.DEVNULL
        ).decode().strip()
        m = re.match(r"v?(\d+)\.(\d+)\.(\d+)", out)
        if m:
            return int(m.group(1)), int(m.group(2)), int(m.group(3))
    except Exception:
        pass
    return 0, 0, 0

major, minor, patch = get_git_version()
print(f"Firmware version from git: {major}.{minor}.{patch}")

env.Append(CPPDEFINES=[
    ("APP_FW_MAJOR", major),
    ("APP_FW_MINOR", minor),
    ("APP_FW_PATCH", patch),
])
