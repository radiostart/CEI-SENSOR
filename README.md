# Mellow Air — Craft Environment Indicator (CEI-SENSOR)

제과제빵 환경의 온도/습도를 실시간 모니터링하는 IoT 센서 기기 펌웨어.
발효, 숙성, 건조 등 공정별 목표 환경을 추적하고 e-Paper 디스플레이에 표시합니다.

## 하드웨어 구성

- **MCU**: ESP32-C3-MINI-1 (RISC-V, BLE 5.0)
- **센서**: SHT-45 (온도/습도, I2C)
- **디스플레이**: 2.13" e-Paper (122x250px, SSD1680, SPI)
- **전원**: Li-ion 배터리 + USB Type-C 충전 (BQ24075)
- **PCB**: 커스텀 2-layer PCB (71.5x33mm)
- **회로도**: [CEI-TOOL](../CEI-TOOL) (KiCad)

## 주요 기능

- 10초 간격 온습도 측정 및 e-Paper 표시
- BLE GATT Server — 모바일 앱([CEI-APP](../CEI-APP))과 실시간 통신
- 공정 모니터링 (목표 온도/습도, 허용 편차, 진행률, 남은 시간)
- SPIFFS 링 버퍼 데이터 로깅 (24시간, 8640 레코드)
- 배터리 최적화 (light sleep, 적응형 슬립 주기, BLE TX 파워 조절)
- 버튼 제어: 단일 클릭(즉시 갱신), 더블클릭(BLE 페어링), 3초 홀드(전원 OFF)

## 공정 모니터링

앱(CEI-APP)에서 공정명, 목표 온도/습도, 허용 편차, 공정 시간을 설정하면 기기가 BLE를 통해 수신하여 실시간으로 환경을 추적합니다. 기기 자체에는 공정 프리셋이 없으며, 모든 공정 설정은 앱에서 전송됩니다.

- 현재 환경이 목표 허용 범위를 벗어나면 e-Paper에서 해당 값을 반전 표시로 경고
- 공정 미설정 시 경과 시간(Elapsed HH:MM)만 표시
- 앱 미연결 시에도 마지막 수신한 공정 설정으로 독립 동작 (NVS 저장)

## GPIO 핀 맵

| GPIO | 기능 | 상세 |
|------|------|------|
| GPIO0 | I2C_SDA | SHT-45 센서 데이터 (4.7k 풀업) |
| GPIO1 | I2C_SCL | SHT-45 센서 클럭 (4.7k 풀업) |
| GPIO2 | BAT_ADC | 배터리 전압 측정 (47k+47k 분압) |
| GPIO3 | MAIN_BTN | 메인 버튼 (RTC GPIO, 딥슬립 웨이크업) |
| GPIO4 | SPI_RST | e-Paper 리셋 |
| GPIO5 | SPI_CS | e-Paper 칩 셀렉트 |
| GPIO6 | SPI_CLK | e-Paper SPI 클럭 |
| GPIO7 | SPI_MOSI | e-Paper SPI 데이터 |
| GPIO10 | USB_PGOOD | BQ24075 USB 전원 감지 (LOW=연결) |
| GPIO20 | SPI_BUSY | e-Paper BUSY |
| GPIO21 | SPI_DC | e-Paper 데이터/명령 선택 |

## 빌드 및 플래시

### 빌드

```bash
pio run
```

### 플래시 (OpenOCD JTAG)

ESP32-C3 네이티브 USB-CDC에서는 esptool이 동작하지 않습니다.
OpenOCD JTAG 방식을 사용합니다:

1. **다운로드 모드 진입**: BOOT 버튼 누른 채 USB 연결 → 놓기
2. **플래시**: `sudo bash flash.sh`

또는 PlatformIO 업로드 (다운로드 모드 필요):
```bash
pio run -t upload
```

### 시리얼 모니터

```bash
pio device monitor
```

## 설정

### app_config.h

```c
#define APP_ENABLE_BLE 1        // BLE GATT Server (0=비활성화)
#define APP_USE_DUMMY_SENSOR 0  // 더미 센서 모드 (1=테스트용)
```

### 배터리 절전 설정

| 항목 | 값 | 설명 |
|------|-----|------|
| 활성 슬립 | 10초 | 공정 진행 중 또는 BLE 연결 |
| 유휴 슬립 | 30초 | 공정 미실행 + BLE 미연결 |
| BLE TX (USB) | +9 dBm | 빠른 탐색 |
| BLE TX (배터리) | +3 dBm | 절전 |
| 로그 (USB) | INFO | 디버깅용 |
| 로그 (배터리) | WARN | UART 절전 |

## BLE GATT 프로토콜

**Service UUID**: `BA5E0001-0000-1000-8000-00805F9B34FB`

| Characteristic | UUID | 속성 | 용도 |
|----------------|------|------|------|
| REALTIME_DATA | BA5E0002 | Notify | 온습도 + 타임스탬프 (10초) |
| UNSENT_DATA | BA5E0003 | Indicate+Write | 미전송 로그 동기화 |
| PROCESS_CONFIG | BA5E0004 | Write | 공정 설정 수신 |
| DEVICE_STATUS | BA5E0005 | Read | 펌웨어 버전, 저장소, 업타임 |
| DEVICE_NAME | BA5E0006 | Read/Write | 기기 이름 |
| ELAPSED_SYNC | BA5E0007 | Write NR | 앱 경과 시간 동기화 |

## 프로젝트 구조

```
CEI-SENSOR/
├── include/
│   ├── app_config.h          # 전역 설정 (핀, 타이밍, 기능 플래그)
│   ├── sht45.h               # SHT-45 센서 드라이버
│   ├── epd_driver.h          # e-Paper 드라이버 (SSD1680)
│   ├── epd_ui.h              # e-Paper UI 레이어
│   ├── display_service.h     # 디스플레이 서비스
│   ├── sensor_service.h      # 센서 서비스
│   ├── sensor_glyphs_24px.h  # 센서 값 비트맵 폰트
│   ├── power_manager.h       # 전원/슬립 관리
│   ├── battery_monitor.h     # 배터리 모니터 (ADC)
│   ├── ble_server.h          # BLE GATT Server
│   ├── process_context.h     # 공정 컨텍스트 (NVS)
│   └── spiffs_logger.h       # SPIFFS 링 버퍼 로거
├── src/
│   ├── main.c                # 메인 상태 머신
│   ├── sht45.c               # SHT-45 드라이버
│   ├── epd_driver.c          # e-Paper 드라이버
│   ├── epd_ui.c              # e-Paper UI
│   ├── display_service.c     # 디스플레이 서비스
│   ├── sensor_service.c      # 센서 서비스 (히터 포함)
│   ├── sensor_glyphs_24px.c  # 비트맵 글리프 데이터
│   ├── power_manager.c       # 전원 관리
│   ├── battery_monitor.c     # 배터리 ADC
│   ├── ble_server.c          # BLE GATT 구현
│   ├── process_context.c     # NVS 공정 컨텍스트
│   ├── spiffs_logger.c       # SPIFFS 로거
│   └── CMakeLists.txt        # 빌드 설정
├── platformio.ini            # PlatformIO 설정
├── sdkconfig.defaults        # ESP-IDF 설정
├── partitions.csv            # 파티션 테이블 (1MB app + 960KB SPIFFS)
├── openocd_upload.py         # OpenOCD 업로드 스크립트
└── claude.md                 # Claude Code 프로젝트 문서
```

## 연관 프로젝트

- **[CEI-APP](../CEI-APP)** — Flutter 모바일 앱 (BLE GATT Client)
- **[CEI-TOOL](../CEI-TOOL)** — KiCad 회로/PCB 설계

## 문제 해결

### 센서가 감지되지 않음
1. I2C 케이블 연결 확인 (J4 커넥터: 3.3V, GND, SDA, SCL)
2. 더미 센서 모드로 동작 확인: `app_config.h`에서 `APP_USE_DUMMY_SENSOR 1`

### 플래시가 안됨
1. 다운로드 모드 진입 확인 (BOOT 버튼 + USB 연결)
2. `sudo` 필요 (macOS libusb 권한)
3. `flash.sh` 또는 `pio run -t upload` 사용

### BLE 연결이 안됨
1. 더블클릭으로 페어링 모드 진입 (30초간 Fast Advertising)
2. 배터리 모드에서는 Slow Advertising (800-1600ms) — 탐색에 시간 소요

## 라이선스

MIT License
