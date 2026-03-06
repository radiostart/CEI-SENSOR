# BakeTrack — 구현 프롬프트 (한국어)

---

## 1. 펌웨어 프롬프트 (ESP32-C3 / ESP-IDF)

```
당신은 임베디드 펌웨어 엔지니어입니다. ESP32-C3와 ESP-IDF 프레임워크(Arduino 미사용)를 기반으로 BakeTrack IoT 센서 기기의 펌웨어를 구현하세요. 이 기기는 제과제빵 환경의 온도와 습도를 모니터링하고 2.13인치 e-Paper 디스플레이에 데이터를 표시합니다.

---

## 하드웨어 사양

- MCU: ESP32-C3-MINI-1 (Espressif, RISC-V 160MHz)
- 프레임워크: ESP-IDF v5.4.1 (PlatformIO, Arduino 미사용)
- 센서: SHT45, I2C 통신 (메인 PCB에 4.7kΩ 풀업 저항 설치)
- 디스플레이: 2.13인치 e-Paper BW (250×122px, SSD1680 컨트롤러), SPI 연결
- 저장소: SPIFFS (데이터 로깅용 960KB 파티션)
- 통신: BLE (Bluetooth Low Energy, GATT Server, Bluedroid)
- 플래시: 2MB (DIO 80MHz)
- 배터리: 1-cell Li-ion 3.7V (BMS 내장 배터리 팩 사용)
- 충전: BQ24075 (USB 500mA, Power Path)

---

## 아키텍처

**싱글 루프 상태 머신** 방식. 별도 FreeRTOS 태스크를 사용하지 않고, `app_main()` 내 단일 while 루프에서 상태별 처리.

```
app_main()
  └─ while(1)
       ├─ APP_STATE_ACTIVE → handle_active_state()
       │    ├─ BLE 페어링 모드 확인 (더블클릭)
       │    ├─ BLE 연결 해제 감지 → 상태 정리
       │    ├─ 센서 읽기 (I2C init → read → deinit)
       │    ├─ SPIFFS 로깅
       │    ├─ BLE REALTIME_DATA notify
       │    ├─ 앱 공정 설정 수신 확인
       │    ├─ Elapsed Sync 보정
       │    ├─ 저전압 체크 (6사이클마다)
       │    ├─ USB 상태 변경 감지
       │    ├─ 디스플레이 갱신 (유의미 변화 or 공정 진행 중)
       │    ├─ BLE 브로드캐스트 창 (3초)
       │    └─ 슬립 진입 (light sleep / vTaskDelay)
       │
       └─ APP_STATE_OFF → power_manager_enter_off_mode()
            └─ 딥슬립 진입 (GPIO3 버튼 웨이크업만)
```

---

## GPIO 핀 맵 (KiCad 회로도 기준)

| GPIO | 신호명 | 기능 | 회로 상세 |
|------|--------|------|-----------|
| GPIO0 | I2C_SDA | SHT-45 센서 데이터 | 4.7kΩ 풀업 (R10), JST 4핀 커넥터 (J4) |
| GPIO1 | I2C_SCL | SHT-45 센서 클럭 | 4.7kΩ 풀업 (R8), JST 4핀 커넥터 (J4) |
| GPIO2 | BAT_ADC | 배터리 전압 ADC (ADC1_CH2) | 분압기: R2(47kΩ)+R1(47kΩ), R15(1kΩ) 직렬 보호, C11(0.47µF) 필터 |
| GPIO3 | MAIN_BTN | 메인 버튼 (Active LOW) | RTC GPIO, 딥슬립 웨이크업 지원 |
| GPIO4 | SPI_RST | E-Paper 리셋 | 47kΩ 풀업 (R17 → 3.3V) |
| GPIO5 | SPI_CS | E-Paper 칩 셀렉트 | 47kΩ 풀업 (R16) |
| GPIO6 | SPI_CLK | E-Paper SPI 클럭 | |
| GPIO7 | SPI_MOSI | E-Paper SPI 데이터 | |
| GPIO8 | BOOT_STRAP | 부트 모드 선택 | 10kΩ 풀업 (R22), SPI 부트 기본값 |
| GPIO9 | BOOT_BTN | 다운로드 모드 버튼 (S3) | Active LOW, KMR211NGLFS |
| GPIO10 | USB_PGOOD | BQ24075 전원 양호 신호 | Open-drain, 47kΩ 외부 풀업, LOW=USB 연결 |
| GPIO18 | USB_D- | USB Type-C 데이터 | ESD 보호: PESD5V0S2BT (D2) |
| GPIO19 | USB_D+ | USB Type-C 데이터 | ESD 보호: PESD5V0S2BT (D2) |
| GPIO20 | SPI_BUSY | E-Paper 비지 신호 | HIGH=비지 (EPD_BUSY_IS_HIGH=1) |
| GPIO21 | SPI_DC | E-Paper 데이터/명령 선택 | |

> GPIO11~17은 ESP32-C3-MINI-1 모듈 내부 SPI 플래시 전용으로 외부 사용 불가
> 사용 가능한 GPIO를 전부 사용 중 — 여유 핀 없음

### 배터리 전압 측정 회로 (GPIO2)
- 분압비: VBAT × (47k / (47k+47k)) = VBAT / 2
- R15 (1kΩ): ADC 입력 보호용 직렬 저항
- C11 (0.47µF): 노이즈 필터 (RC 로우패스)
- 최대 배터리 4.2V → ADC 핀 2.1V (12dB 감쇠 범위 내)
- ADC 캘리브레이션: Curve Fitting 방식 (ESP32-C3 지원)
- 펌웨어 보상: `battery_voltage = voltage_mv * 2`

### I2C 센서 커넥터 (J4 — SM04B-SRSS-TB)
- Pin 1: +3.3V (C13 1µF 바이패스)
- Pin 2: GND
- Pin 3: I2C_SDA (GPIO0)
- Pin 4: I2C_SCL (GPIO1)

### 충전 IC (BQ24075RGTT)
- USB_PGOOD (GPIO10): USB 전원 양호 시 LOW 출력 (open-drain)
- 충전 전류: USB 기본 500mA (ISET 저항 R5=1.8kΩ으로 설정)
- 배터리 보호: BMS 내장 배터리 팩 사용 (외부 보호 회로 불필요)

### 부트 모드
- 일반 부트: GPIO8 HIGH (10kΩ 풀업) + GPIO9 HIGH → SPI 플래시 부트
- 다운로드 모드: BOOT 버튼(S3)으로 GPIO9 LOW → USB-CDC 펌웨어 업로드
- EN 버튼(S1): 칩 리셋 (CHIP_PU를 LOW로 토글)

---

## e-Paper 디스플레이 레이아웃

패널: 250×122px BW (SSD1680), 세로형(Portrait) 배치
갱신 주기: 부분 갱신(partial refresh) 방식으로 10초마다 갱신

```
┌─────────────────────────┐ Y=0
│ TOP BAR  [BT] [BAT]     │ 24px (BLE/충전/배터리 아이콘)
├─────────────────────────┤ Y=25
│ TEMP         HUMIDITY   │
│ 24.5°C       68.0%      │ 86px (큰 폰트, VALUES)
├─────────────────────────┤ Y=114
│ Target                   │
│ 25.0C / 70%              │ 80px (작은 폰트, TARGET/DIFF)
│ Diff                     │
│ -0.5C / -2.0%            │
├─────────────────────────┤ Y=197
│ PROOFING   42:30 left   │
│ [=====>       ] 55%     │ 53px (작은 폰트, PROOFING)
└─────────────────────────┘ Y=250
```

표시 규칙:
- 현재값이 목표 허용 범위를 벗어나면 해당 줄을 흑백 반전으로 경고 표시
- 앱에서 공정이 설정되지 않은 경우 프로그레스 바 영역을 Elapsed: HH:MM 로 대체
- 남은 시간: `Xm left` 또는 `Xh XXm left` (분 단위 올림)
- 진행률: 10초 단위 절삭
- 디스플레이는 영문만 사용 (한글 폰트 미포함)
- 부분 갱신 사용으로 깜빡임 최소화 및 소비전력 절감
- N회 부분 갱신마다 전체 갱신 실행 (APP_EPD_FULL_REFRESH_INTERVAL=10)
- Top Bar: BLE 연결 아이콘, USB 충전 아이콘, 배터리 잔량 아이콘 표시
- 특수 화면: 저전압 경고, 전원 OFF, BLE 페어링 모드 (카운트다운)

---

## 기능 목록

### 1. 센서 (SHT45 / I2C)
- [x] ESP-IDF i2c 드라이버를 사용하여 SHT45 I2C 초기화
- [x] 10초 간격으로 온도 및 습도 읽기
- [x] 결로 방지를 위한 SHT45 내장 히터 루틴 구현 (고출력, 200ms 이하 작동, 6시간 주기)
- [x] 히터 작동 후 I2C 해제 → 2초 안정화 대기 → I2C 재초기화 후 측정 수행
- [x] 온도가 80°C 이상일 때 오븐 경고 상태 플래그 설정
- [x] I2C 사이클별 init/deinit (저전력): 센서 읽기 전 초기화 → 읽기 후 해제
- [x] 센서 주소 자동 스캔 (0x44, 0x45 시도, 최초 1회 캐싱)
- [x] 유의미한 변화 감지: 온도 ±0.2°C 또는 습도 ±1.5% 이상 시 디스플레이 갱신

### 2. e-Paper 디스플레이 (SPI)
- [x] ESP-IDF spi_master 드라이버를 사용하여 2.13인치 e-Paper SPI 초기화
- [x] 10초 갱신 주기에 부분 갱신(partial refresh) 적용
- [x] 영문 전용 비트맵 폰트로 위 레이아웃 렌더링 (Small 6×8, Medium, Large)
- [x] TEMP / HUMIDITY 현재값은 큰 폰트 사용
- [x] Target, Diff, 공정명, 타이머, 프로그레스 바는 작은 폰트 사용
- [x] Diff 값이 허용 편차 초과 시 해당 줄 반전 렌더링
- [x] ASCII 프로그레스 바 렌더링: [=====>        ] XX%
- [x] 진행률 계산: (현재시각 - start_time) / duration_min
- [x] 공정 미설정 상태에서는 Elapsed: HH:MM 표시
- [x] 온도 80°C 이상 감지 시 WARN: HIGH TEMP 즉시 표시
- [x] 공정 완료 시: 전체 갱신 2회 깜빡임 (시각적 알림)
- [x] 디스플레이 sleep/wakeup 제어 (저전력)

### 3. 데이터 로깅 (SPIFFS)
- [x] 부팅 시 SPIFFS 파티션 초기화
- [x] 각 센서 읽기 값을 레코드로 저장: { timestamp (unix), temp (float), humidity (float), sent (bool) }
- [x] 저장소가 가득 차면 가장 오래된 레코드부터 덮어쓰는 링 버퍼 방식 적용
- [x] 최대 보관량: 10초 간격 기준 24시간치 (8,640개 레코드, 약 135KB)
- [x] 공정 컨텍스트를 NVS에 저장: { process_name, target_temp, target_humi, tolerance_temp, tolerance_humi, duration_min, start_time }
- [x] 재부팅 시 NVS에서 공정 컨텍스트 복원하여 앱 연결 없이도 디스플레이 정상 동작

### 4. BLE (GATT Server)
- [x] ESP-IDF Bluedroid로 BLE 스택 초기화
- [x] GATT Server로 동작하며 상시 advertisement 유지
- [x] TX 출력: +9 dBm (ADV, SCAN, DEFAULT 모든 타입)
- [x] 연결 파라미터: interval 30-50ms, latency 0, timeout 4000ms (iOS 호환)
- [x] Advertising 전략:
    - USB 연결 시: Fast advertising (100-200ms interval)
    - 배터리 모드: Slow advertising (800-1600ms interval, 절전)
    - 페어링 모드: Fast advertising (30초 한정)
- [x] BLE 컨트롤러 슬립: `esp_bt_sleep_enable()` (Bluedroid 초기화 완료 후 호출)
- [x] Advertising 재시작: stop→callback→restart 패턴으로 안전한 파라미터 전환
- [x] 유령 연결 감지: 연결됐지만 구독 없으면 3사이클(30초) 후 강제 해제
- [x] 아래 GATT Characteristic 정의 및 구현:

  REALTIME_DATA (Notify, UUID: BA5E0002, 16 bytes)
    → 앱 연결 상태에서 10초마다 현재 온도, 습도, 타임스탬프 전송
    → CCCD 구독 감지 시 즉시 센서 읽기 및 전송 (initial sync)
    → 패킷: { uint32 timestamp, int16 temp_x10, int16 humi_x10, uint8 flags, pad[3], uint32 elapsed_sec }

  UNSENT_DATA (Indicate, UUID: BA5E0003, 20 bytes/chunk)
    → 앱 연결 시 sent == false 인 레코드 전체 조회
    → 시간 순서대로 청크 단위(20바이트 패킷)로 전송
    → 청크별로 앱의 ACK 수신 후 다음 청크 전송
    → ACK 수신 완료된 레코드만 sent == true 로 갱신
    → 전송 중 연결 끊김 시 마지막 ACK 지점부터 재개

  PROCESS_CONFIG (Write, UUID: BA5E0004, 56 bytes)
    → 앱에서 공정 설정값 수신:
      { process_name[32], target_temp (float), target_humi (float),
        tolerance_temp (float), tolerance_humi (float),
        duration_min (uint16), _pad (uint16), start_time (uint32) }
    → 수신 즉시 NVS에 저장
    → 수신 즉시 디스플레이 갱신 트리거

  DEVICE_STATUS (Read, UUID: BA5E0005, 48 bytes)
    → 펌웨어 버전, 저장소 사용량/전체, 현재 공정명, 업타임 제공
    → 패킷: { uint8 major, uint8 minor, uint16 patch, uint32 used, uint32 total, name[32], uint32 uptime }

  DEVICE_NAME (Read/Write, UUID: BA5E0006)
    → 기기 이름 읽기/변경 (최대 20자, NVS 저장)

  ELAPSED_SYNC (Write Without Response, UUID: BA5E0007, 5 bytes)
    → 앱의 공정 경과 시간(초) + 일시정지 플래그 수신
    → 패킷: { uint32_t elapsed_sec (LE), uint8_t flags (bit0=paused) }
    → 앱이 주도권: 기기가 뒤처질 때만 보정, 앞서면 로컬 유지
    → BLE 수신 시각 기준 처리 지연 자동 보상 (s_elapsed_sync_rx_us)
    → PROCESS_CONFIG와 함께 전송 시 시작값으로 반영 (0 리셋 방지)
    → 앱 미연결 시 기기 로컬 타이머 독립 진행

- [x] BLE 연결/해제 이벤트 발생 시 센서 및 디스플레이 루프 중단 없이 처리
- [x] BLE 재연결 시 활성 공정 자동 동기화 (앱→기기)
- [x] BLE 연결 해제 시 경과 시간 NVS 저장 + 일시정지 해제

### 5. 버튼 동작 (GPIO3 — MAIN_BTN)
- [x] **단일 클릭**: 즉시 센서 읽기 + 디스플레이 갱신 + BLE 데이터 전송
- [x] **더블 클릭** (400ms 이내): BLE 페어링 모드 진입
    - 30초간 Fast advertising으로 앱 연결 대기
    - E-Paper에 페어링 화면 표시 (남은 시간, 5초마다 갱신)
    - 앱 연결 성공, 타임아웃, 또는 버튼 재클릭 시 모드 종료
- [x] **3초 장기 누름**: 전원 끄기 (딥슬립 진입)
- [x] **전원 ON**: 딥슬립에서 버튼 1초 홀드로 기동 (APP_BUTTON_POWERON_MS=1000)

### 6. 즉시 동기화 (Initial Sync)
- [x] 앱이 REALTIME_DATA CCCD를 구독하면 `s_initial_sync_pending` 플래그 설정
- [x] main.c/power_manager.c의 대기 루프에서 플래그 감지 시 즉시 센서 사이클 실행
- [x] 앱 연결 직후 데이터 전송 지연 없이 즉시 동기화

### 7. 전원 관리
- [x] Light Sleep: 배터리 모드에서 10초 슬립 (타이머 + 버튼 + USB 웨이크업)
- [x] USB 연결 시: light sleep 대신 vTaskDelay 사용 (JTAG 안정성)
- [x] Deep Sleep OFF 모드: GPIO3 버튼 웨이크업만 (GPIO10은 RTC GPIO 아님)
- [x] RTC_DATA_ATTR 플래그로 딥슬립 OFF 모드 상태 유지
- [x] 저전압 자동 종료: 배터리 3300mV 미만 시 자동 딥슬립 (6사이클=60초마다 체크)
- [x] 저전압 복귀 임계값: 3500mV 이상이어야 전원 ON 허용
- [x] USB 상태 변경 감지 → 즉시 디스플레이 갱신 트리거
- [x] BLE 연결 중: 슬립 대신 vTaskDelay로 연결 유지

### 8. 시스템 및 안전
- [x] 80°C 임계값 감지 시:
    - 경고 플래그 설정
    - 다음 10초 주기 대기 없이 즉시 디스플레이 갱신
    - 경고 플래그가 포함된 REALTIME_DATA notify 즉시 전송
- [x] 경과 시간 NVS 주기 저장 (10사이클 = 100초마다)
- [x] 부팅 시 공정 컨텍스트 IDLE 상태로 초기화 (앱 연결 시 PROCESS_CONFIG로 복원)

---

## 구현 참고사항

- ESP-IDF v5.4.1 API 사용 (PlatformIO esp32-c3-devkitm-1 보드)
- 모든 영구 데이터는 딥슬립 및 전원 차단 후에도 NVS를 통해 유지
- I2C 클럭: 50kHz (APP_I2C_FREQ_HZ), 케이블 최대 1.5m 대응
- I2C 사이클별 init/deinit: 센서 읽기 전 초기화 → 읽기 후 해제 (저전력)
- e-Paper SPI 클럭: 제조사 스펙 준수 (일반적으로 2~4MHz)
- e-Paper 패널: EPD_RAM_WIDTH=128, EPD_PANEL_WIDTH=122, EPD_HEIGHT=250
- SPIFFS 레코드 포맷은 저장 효율을 위해 바이너리 패킹 사용 (16 bytes/record)
- BLE MTU는 청크 전송 속도 극대화를 위해 협상(negotiate) 처리
- BLE TX 출력: +9 dBm (ESP_PWR_LVL_P9), ADV/SCAN/DEFAULT 모든 타입
- BLE 컨트롤러 슬립은 Bluedroid init 완료 후 활성화
- USB 연결 시 light sleep 대신 vTaskDelay 사용 (JTAG 안정성)
- 공정 설정 수신 시 즉시 디스플레이 갱신 (power_manager_request_update)
- 디스플레이 시간 표시: 남은 시간 `Xm left` / `Xh XXm left` (분 단위 올림), 경과 시간 `HH:MM`
- 진행률: 10초 단위 절삭 `(elapsed/10)*10 * 100 / duration`
- 공정 완료 시: 전체 갱신 2회 깜빡임 (시각적 알림)
- 전원 OFF: 딥슬립 (GPIO3 버튼 웨이크업만, GPIO10은 RTC GPIO 아님)
- 전원 ON: 딥슬립에서 1초 홀드 필요, 저전압 시(< 3500mV) 재진입

### 경과 시간 동기화 (Elapsed Sync) 아키텍처

- 기기 로컬 타이머가 기본 (esp_timer 기반, 매 10초 독립 진행)
- 앱 연결 시: ELAPSED_SYNC(BA5E0007) 수신 → 기기가 뒤처질 때만 보정
- PROCESS_CONFIG 수신 시: 함께 도착한 ELAPSED_SYNC 값을 시작값으로 사용
- BLE 수신 시각(s_elapsed_sync_rx_us)을 기준점으로 사용하여 처리 지연 자동 보상
- 일시정지/재개: 항상 앱 값으로 보정 (s_process_paused 플래그)
- 앱 미연결: 마지막 elapsed 기준으로 로컬 계속 카운팅, NVS 주기 저장 (100초)

### 파티션 테이블 (2MB Flash)

| Name | Type | Offset | Size |
|------|------|--------|------|
| nvs | data/nvs | 0x9000 | 20KB |
| phy_init | data/phy | 0xE000 | 4KB |
| factory | app | 0x10000 | 1MB |
| spiffs | data/spiffs | 0x110000 | 960KB |

### sdkconfig 주요 설정

- Flash: 2MB, DIO, 80MHz
- BLE: Bluedroid, GATTS only (GATTC 비활성), BLE 4.2
- Console: UART + USB JTAG secondary
- Log: INFO 레벨

---

## 파일 구조

```
CEI-SENSOR/
├── platformio.ini             # PlatformIO 프로젝트 설정
├── partitions.csv             # 커스텀 파티션 테이블
├── sdkconfig.defaults         # ESP-IDF sdkconfig 기본값
├── claude.md                  # 이 파일 (프로젝트 문서)
├── include/
│   ├── app_config.h           # 전역 설정 (핀맵, 임계값, 구조체)
│   ├── battery_monitor.h      # 배터리 전압/USB 감지
│   ├── ble_server.h           # BLE GATT Server + 패킷 구조체
│   ├── button_handler.h       # 버튼 이벤트 (short/long/double)
│   ├── display_service.h      # 디스플레이 상위 서비스
│   ├── epd_driver.h           # SSD1680 E-Paper 저수준 드라이버
│   ├── epd_font.h             # 비트맵 폰트 정의
│   ├── epd_icons.h            # 아이콘 비트맵 (BLE, 배터리 등)
│   ├── epd_ui.h               # E-Paper UI 레이아웃 + 상태 구조체
│   ├── i2c_manager.h          # I2C 버스 init/deinit 관리
│   ├── power_manager.h        # 전원 상태 머신 + 슬립 관리
│   ├── process_context.h      # 공정 컨텍스트 NVS 저장/복원
│   ├── sensor_glyphs_24px.h   # 24px 센서 아이콘 글리프
│   ├── sensor_service.h       # 센서 읽기 서비스
│   ├── sht45.h                # SHT45 I2C 드라이버
│   └── spiffs_logger.h        # SPIFFS 링 버퍼 로거
└── src/
    ├── main.c                 # 메인 루프 + 상태 머신
    ├── battery_monitor.c      # ADC 배터리 측정 + USB PGOOD
    ├── ble_server.c           # BLE GATT 구현
    ├── button_handler.c       # 버튼 디바운스 + 이벤트 감지
    ├── display_service.c      # 디스플레이 서비스 구현
    ├── epd_driver.c           # SSD1680 SPI 드라이버
    ├── epd_font.c             # 비트맵 폰트 데이터
    ├── epd_icons.c            # 아이콘 비트맵 데이터
    ├── epd_ui.c               # UI 렌더링 구현
    ├── i2c_manager.c          # I2C 버스 관리
    ├── power_manager.c        # 전원/슬립 관리
    ├── process_context.c      # NVS 공정 컨텍스트
    ├── sensor_glyphs_24px.c   # 글리프 데이터
    ├── sensor_service.c       # SHT45 센서 서비스
    ├── sht45.c                # SHT45 드라이버
    └── spiffs_logger.c        # SPIFFS 로거
```

---

## 연관 프로젝트

### BakeTrack 모바일 앱 (CEI-APP)
- **경로**: `../CEI-APP`
- **프레임워크**: Flutter (iOS + Android)
- **BLE 라이브러리**: flutter_blue_plus v1.32.0
- **역할**: BLE GATT Client — 이 펌웨어(GATT Server)와 통신하여 실시간 모니터링, 데이터 동기화, 공정 설정 전송을 수행
- **BLE 프로토콜 공유**: 두 프로젝트는 동일한 GATT Service/Characteristic UUID(BA5E0001~0007)와 패킷 포맷을 사용. 한쪽을 변경하면 반드시 다른 쪽도 동기화 필요
- **앱의 claude.md**: `../CEI-APP/claude.md`

### BakeTrack 하드웨어 회로도 (CEI-TOOL)
- **경로**: `../CEI-TOOL`
- **도구**: KiCad 9.0
- **회로도**: `../CEI-TOOL/CEI-TOOL.kicad_sch` — GPIO 핀 매핑, 전압 분배기, 충전 IC(BQ24075) 회로 등 하드웨어 설계 참조
- **PCB**: `../CEI-TOOL/CEI-TOOL.kicad_pcb` — 71.51×33mm, 2-layer FR4
- **하드웨어 claude.md**: `../CEI-TOOL/CLAUDE.md`
