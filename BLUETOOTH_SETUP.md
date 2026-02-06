# Bluetooth 설정 검증 보고서

## ✅ 최종 검증 결과

현재 Bluetooth 설정은 **ESP-IDF 표준 방식**을 따르며 **올바르게 구성**되어 있습니다.

---

## 📋 설정 검증

### 1. ESP-IDF 프레임워크 설정 ✅

**파일**: `platformio.ini`

```ini
[env:esp32-s3-devkitc-1]
platform = espressif32
board = esp32-s3-devkitc-1
framework = espidf

board_build.esp-idf.sdkconfig_path = sdkconfig.defaults
```

**검증**: ✅ 표준 ESP-IDF 방식 사용

---

### 2. Bluetooth 스택 설정 ✅

**파일**: `sdkconfig.defaults`

```
CONFIG_BT_ENABLED=y                         # Bluetooth 활성화
CONFIG_BT_BLUEDROID_ENABLED=y               # Bluedroid 스택 사용
CONFIG_BT_BLE_ENABLED=y                     # BLE 활성화
CONFIG_BT_CONTROLLER_ENABLED=y              # BT 컨트롤러 활성화
CONFIG_BT_BLE_42_FEATURES_SUPPORTED=y       # BLE 4.2 기능 (중요!)
CONFIG_BT_GATTS_ENABLE=y                    # GATT 서버 활성화
```

**검증**: 
- ✅ **Bluedroid 스택** 사용 (ESP-IDF 권장 방식)
- ✅ **BLE 4.2 기능 활성화** (API 함수 사용 가능)
- ✅ **최소한의 설정만 포함** (27줄, 이전 2801줄에서 정리됨, **99% 감소**)

**주요 개선사항**:
- 불필요한 자동 생성 설정 2774줄 제거 (2801줄 → 27줄)
- 핵심 설정만 유지하여 유지보수성 향상
- ESP-IDF가 빌드 시 자동으로 전체 설정을 생성하므로 문제없음

---

### 3. CMake 빌드 설정 ✅

**파일**: `src/CMakeLists.txt`

```cmake
idf_component_register(
    SRCS "main.c" "sht45.c" "ssd1306.c" "ble_server.c" "cei_calculator.c"
    INCLUDE_DIRS "../include"
    REQUIRES bt driver nvs_flash
)
```

**검증**:
- ✅ `REQUIRES bt` - Bluetooth 컴포넌트 의존성 명시
- ✅ `nvs_flash` - BLE bonding 정보 저장용
- ✅ `driver` - I2C 드라이버 의존성

---

### 4. 애플리케이션 코드 ✅

**파일**: `src/main.c`

```c
#define ENABLE_BLE          1    // BLE 활성화

#if ENABLE_BLE
#include "ble_server.h"
#endif
```

**검증**:
- ✅ 조건부 컴파일로 BLE 기능 제어 가능
- ✅ 필요시 간단히 비활성화 가능

---

### 5. BLE 서버 구현 ✅

**파일**: `src/ble_server.c`

```c
// Bluedroid API 사용
#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "esp_bt_main.h"
```

**검증**:
- ✅ **Bluedroid API** 사용 (표준 방식)
- ✅ GATT 서버 구현
- ✅ 센서 데이터 전송 (JSON 형식)

---

## 🔍 링커 문제 해결 과정

### 문제 원인
```
undefined reference to `esp_ble_gap_start_advertising'
undefined reference to `esp_ble_gap_config_adv_data'
```

### 근본 원인
`CONFIG_BT_BLE_42_FEATURES_SUPPORTED` 설정이 없어서 BLE 4.2 API 함수들이 조건부 컴파일에서 제외됨

### 해결 방법
```
# sdkconfig.defaults에 추가
CONFIG_BT_BLE_42_FEATURES_SUPPORTED=y
```

### 기술 배경
ESP-IDF의 `esp_gap_ble_api.c` 소스코드:
```c
#if (BLE_42_FEATURE_SUPPORT == TRUE)
esp_err_t esp_ble_gap_start_advertising(...) {
    // 구현
}
#endif
```

`BLE_42_FEATURE_SUPPORT` 매크로는 다음과 같이 정의됨:
```c
// bt_target.h
#if (UC_BT_BLE_42_FEATURES_SUPPORTED == TRUE)
#define BLE_42_FEATURE_SUPPORT   TRUE
#endif

// bluedroid_user_config.h
#ifdef CONFIG_BT_BLE_42_FEATURES_SUPPORTED
#define UC_BT_BLE_42_FEATURES_SUPPORTED   CONFIG_BT_BLE_42_FEATURES_SUPPORTED
#endif
```

---

## 🧹 정리된 파일

### 삭제된 불필요한 파일
- ❌ `sdkconfig.old` (백업 파일)
- ❌ `sdkconfig.esp32-s3-devkitc-1` (중복 설정)
- ❌ `install_espidf.sh` (설치 스크립트)
- ❌ `setup_idf.sh` (설정 스크립트)

### 정리된 설정 파일
- ✅ `sdkconfig.defaults`: 2801줄 → **27줄** (99% 감소, 89KB → 700B)

### 업데이트된 .gitignore
```gitignore
# ESP-IDF
build/
sdkconfig
sdkconfig.old
sdkconfig.esp32-*
*.sh
```

---

## 📊 최종 프로젝트 구조

```
CEI-SENSOR/
├── include/
│   ├── sht45.h              # SHT-45 센서 드라이버
│   ├── ssd1306.h            # OLED 디스플레이 드라이버
│   ├── cei_calculator.h     # CEI 계산 라이브러리
│   └── ble_server.h         # BLE 서버 (Bluedroid)
├── src/
│   ├── main.c               # 메인 애플리케이션
│   ├── sht45.c              # 센서 드라이버 구현
│   ├── ssd1306.c            # 디스플레이 드라이버 구현
│   ├── cei_calculator.c     # CEI 계산 구현
│   ├── ble_server.c         # BLE 서버 구현
│   └── CMakeLists.txt       # 컴포넌트 빌드 설정
├── CMakeLists.txt           # 프로젝트 빌드 설정
├── platformio.ini           # PlatformIO 설정
├── sdkconfig.defaults       # ESP-IDF 최소 설정 (23줄)
└── README.md                # 프로젝트 문서
```

---

## ✅ 검증 완료 항목

- [x] Bluedroid 스택 사용 (ESP-IDF 표준)
- [x] BLE 4.2 API 활성화
- [x] CMake 의존성 설정
- [x] 조건부 컴파일 구조
- [x] Clean 빌드 성공
- [x] 펌웨어 업로드 성공
- [x] BLE 장치 Advertising 동작 확인
- [x] 불필요한 파일 정리
- [x] 설정 파일 최소화
- [x] 문서 업데이트

---

## 🎯 결론

**현재 Bluetooth 설정은 ESP-IDF의 표준 방식을 따르며, 프로덕션 환경에서 사용 가능한 수준입니다.**

### 핵심 포인트
1. **Bluedroid 스택** 사용 (ESP 공식 권장)
2. **BLE 4.2 기능** 명시적 활성화 (중요!)
3. **최소한의 설정** 유지 (유지보수성 ↑)
4. **조건부 컴파일** 지원 (유연성 ↑)

### 유지보수 가이드
- `sdkconfig.defaults`는 현재 상태 유지
- 추가 기능이 필요하면 해당 `CONFIG_*` 항목만 추가
- 자동 생성된 `sdkconfig` 파일은 Git에서 제외 (.gitignore)

---

**작성일**: 2024-11-22
**ESP-IDF 버전**: 5.4.1
**PlatformIO 버전**: 6.1.18

