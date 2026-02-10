# CEI-SENSOR 프로젝트

ESP32-S3를 사용한 온습도 센서 및 환경 예측 지수(CEI) 모니터링 시스템입니다.

## 하드웨어 구성

- **MCU**: ESP32-S3 Super Mini
- **센서**: SHT-45 (온도/습도 센서)
- **디스플레이**: 2.13" E-Paper (SSD1680, SPI)
- **통신**: I2C (센서), SPI (디스플레이)

## 기능

- ✅ SHT-45 센서를 통한 온습도 측정
- ✅ 2.13" E-Paper에 실시간 데이터 표시
- ✅ **Coffee Environment Index (CEI)** 계산 및 표시 (1~100 절대 지수)
- ✅ 더미 센서 모드 (센서 없이 테스트 가능)
- ✅ **Bluetooth LE 통신** (앱 연동 가능)

## 연결 방법

### SHT-45 센서
| SHT-45 | ESP32-S3 |
|--------|----------|
| VDD    | 3.3V     |
| SDA    | GPIO 8   |
| SCL    | GPIO 9   |
| GND    | GND      |

### 2.13" E-Paper 디스플레이 (SPI)
| E-Paper | ESP32-C3 |
|---------|----------|
| BUSY    | GPIO 10  |
| RST     | GPIO 3   |
| DC      | GPIO 4   |
| CS      | GPIO 5   |
| CLK     | GPIO 6   |
| DIN     | GPIO 7   |

## 빌드 및 업로드

### PlatformIO 사용 (기본)

```bash
# 빌드
pio run

# 업로드
pio run --target upload

# 시리얼 모니터
pio run --target monitor
```

### ESP-IDF 직접 사용 (Bluetooth 활성화 시 권장)

**ESP-IDF 설치 (최초 1회만)**

```bash
./install_espidf.sh
```

**ESP-IDF 환경 활성화 및 빌드**

```bash
# ESP-IDF 환경 활성화 (매번 새 터미널에서 실행)
source ~/esp/esp-idf/export.sh

# 타겟 설정 (최초 1회만)
idf.py set-target esp32s3

# 빌드 및 업로드
idf.py build
idf.py flash

# 시리얼 모니터
idf.py monitor

# 한 번에 실행
idf.py build flash monitor
```

## 설정 옵션

### src/main.c
```c
// 센서 테스트 모드 (센서 없이 테스트)
#define USE_DUMMY_SENSOR    1    // 1=랜덤값, 0=실제 센서

// BLE 기능 (기본 활성화)
#define ENABLE_BLE          1    // 1=활성화, 0=비활성화
```

**⚠️ 중요**: Bluetooth가 정상 작동하려면 **NVS(Non-Volatile Storage) 초기화**가 필수입니다!
현재 코드에 이미 포함되어 있습니다:
```c
// app_main()의 첫 부분
esp_err_t nvs_ret = nvs_flash_init();
```

### CEI 계산 공식 수정

**CEI (Coffee Environment Index)** 는 카페 실내 환경(온도, 습도)을 1~100점의 절대 지수로 산출합니다.

#### 현재 적용된 공식

```
CEI = ((H - 10) / 80 × 70) + ((T - 10) / 25 × 30) + 1
```

**파라미터:**
- **H** (습도): 유효범위 10~90% (건조주의보 ~ 장마철)
- **T** (온도): 유효범위 10~35°C (겨울 오픈 전 ~ 여름 머신 열기)
- **가중치**: 습도 70점, 온도 30점 (습도가 추출에 더 큰 영향)

#### CEI 등급

| CEI 값 | 등급 | 설명 |
|--------|------|------|
| 1~20   | Very Good | 최적 환경 (원두 보관/추출에 이상적) |
| 21~40  | Good | 양호 (추출에 좋은 환경) |
| 41~60  | Normal | 보통 (일반적인 실내 환경) |
| 61~80  | Caution | 주의 (환경 관리 필요) |
| 81~100 | Bad | 나쁨 (습도/온도 조절 필요) |

#### 공식 커스터마이징

`include/cei_calculator.h` 파일에서 파라미터를 수정할 수 있습니다:

```c
// 습도 유효 범위
#define CEI_HUMIDITY_MIN        10.0f
#define CEI_HUMIDITY_MAX        90.0f

// 온도 유효 범위
#define CEI_TEMPERATURE_MIN     10.0f
#define CEI_TEMPERATURE_MAX     35.0f

// 가중치 (습도:온도 = 7:3)
#define CEI_WEIGHT_HUMIDITY     70.0f
#define CEI_WEIGHT_TEMPERATURE  30.0f
```

더 복잡한 공식이 필요하다면 `src/cei_calculator.c`의 `cei_calculate_custom()` 함수를 수정하세요.

## Bluetooth 기능

Bluetooth Low Energy (BLE) 기능이 **기본적으로 활성화**되어 있습니다.

### 설정 확인

현재 올바르게 설정되어 있습니다:

✅ **sdkconfig.defaults**
```
CONFIG_BT_ENABLED=y
CONFIG_BT_BLUEDROID_ENABLED=y
CONFIG_BT_BLE_ENABLED=y
CONFIG_BT_BLE_42_FEATURES_SUPPORTED=y  # 중요!
```

✅ **src/CMakeLists.txt**
```cmake
REQUIRES bt driver nvs_flash
```

✅ **src/main.c**
```c
#define ENABLE_BLE          1
```

### BLE 비활성화 (선택사항)

BLE 기능이 필요 없다면:

1. `src/main.c`에서 수정:
   ```c
   #define ENABLE_BLE          0
   ```

2. 재빌드:
   ```bash
   pio run
   ```

### BLE 연결 정보
- **디바이스 이름**: CEI-Sensor
- **서비스 UUID**: 4fafc201-1fb5-459e-8fcc-c5c9c331914b
- **센서 데이터 Characteristic UUID**: beb5483e-36e1-4688-b7f5-ea07361b26ab
- **데이터 형식**: JSON
  ```json
  {
    "temperature": 25.5,
    "humidity": 60.0,
    "cei": 20.3
  }
  ```

## 프로젝트 구조

```
CEI-SENSOR/
├── include/
│   ├── app_config.h       # 전역 설정 (핀, 타이밍, 기능 플래그)
│   ├── sht45.h            # SHT-45 센서 드라이버
│   ├── epd_driver.h       # E-Paper 디스플레이 드라이버 (SSD1680)
│   ├── epd_ui.h           # E-Paper UI 레이어
│   ├── display_service.h  # 디스플레이 서비스
│   ├── sensor_service.h   # 센서 서비스
│   ├── power_manager.h    # 전원/슬립 관리
│   ├── battery_monitor.h  # 배터리 모니터
│   ├── cess_calculator.h  # CESS 계산 라이브러리
│   ├── eml_calculator.h   # EML 계산 라이브러리
│   └── ble_server.h       # BLE 서버
├── src/
│   ├── main.c             # 메인 애플리케이션 (상태 머신)
│   ├── sht45.c            # SHT-45 센서 드라이버
│   ├── epd_driver.c       # E-Paper 드라이버 구현
│   ├── epd_ui.c           # E-Paper UI 구현
│   ├── display_service.c  # 디스플레이 서비스
│   ├── sensor_service.c   # 센서 서비스
│   ├── power_manager.c    # 전원 관리
│   ├── battery_monitor.c  # 배터리 모니터
│   ├── cess_calculator.c  # CESS 계산
│   ├── eml_calculator.c   # EML 계산
│   ├── ble_server.c       # BLE 서버 구현
│   └── CMakeLists.txt     # 빌드 설정
├── CMakeLists.txt         # 프로젝트 설정
├── platformio.ini         # PlatformIO 설정
└── sdkconfig.defaults     # ESP-IDF 설정
```

## 문제 해결

### 센서가 감지되지 않음
1. I2C 연결 확인 (SDA, SCL, VCC, GND)
2. 더미 센서 모드로 동작 확인: `app_config.h`에서 `APP_USE_DUMMY_SENSOR 1`

### Bluetooth 컴파일 오류
현재 설정이 올바르게 되어 있어 오류가 발생하지 않아야 합니다.
만약 오류가 발생한다면:
1. Clean 빌드 실행: `pio run -t clean && pio run`
2. `sdkconfig.defaults`에 `CONFIG_BT_BLE_42_FEATURES_SUPPORTED=y` 확인
3. 여전히 문제가 있다면 BLE 비활성화: `#define ENABLE_BLE 0`

## 라이선스

MIT License
