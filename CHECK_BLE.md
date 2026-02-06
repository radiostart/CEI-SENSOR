# 🔍 BLE 동작 확인 가이드

## ✅ 수정 완료 사항

### 1. NVS 초기화 추가 ⭐ **가장 중요!**
```c
// src/main.c에 추가됨
#include "nvs_flash.h"

void app_main(void) {
    // NVS 초기화 (Bluetooth 필수!)
    esp_err_t nvs_ret = nvs_flash_init();
    if (nvs_ret == ESP_ERR_NVS_NO_FREE_PAGES || nvs_ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(nvs_ret);
    // ...
}
```

**왜 필요한가?**
- Bluetooth는 NVS(Non-Volatile Storage)를 사용하여 bonding 정보 저장
- NVS 초기화 없이는 Bluetooth 초기화가 실패함
- **이것이 핸드폰에서 검색이 안 됐던 주요 원인!**

### 2. BLE 기본 활성화
```c
#define ENABLE_BLE          1    // 항상 활성화
```

### 3. 설정 파일 최적화
- `sdkconfig.defaults`: 27줄로 간결화
- 핵심 설정만 유지

---

## 📱 핸드폰에서 BLE 검색하기

### 단계 1: 시리얼 모니터 확인

```bash
pio device monitor --port /dev/cu.usbmodem5ABA0188561 --baud 115200
```

**확인할 로그:**
```
I (xxx) MAIN: NVS 초기화 중...
I (xxx) MAIN: ✅ NVS 초기화 완료!
...
I (xxx) BLE_SERVER: BLE 서버 초기화 중...
I (xxx) BLE_SERVER: ✅ BLE 서버 초기화 완료!
I (xxx) BLE_SERVER: ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
I (xxx) BLE_SERVER: 📱 앱에서 사용할 UUID:
I (xxx) BLE_SERVER:    Service UUID: 4fafc201-1fb5-459e-8fcc-c5c9c331914b
I (xxx) BLE_SERVER:    Data UUID:    beb5483e-36e1-4688-b7f5-ea07361b26ab
I (xxx) BLE_SERVER:    디바이스 이름: CEI-Sensor
I (xxx) BLE_SERVER: ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
I (xxx) BLE_SERVER: ✅ BLE Advertising 시작 (이름: CEI-Sensor)
```

### 단계 2: BLE 스캐너 앱 설치

**iOS:**
- **LightBlue** (추천)
- nRF Connect

**Android:**
- **nRF Connect** (추천)
- BLE Scanner

### 단계 3: 검색하기

1. 앱 실행
2. **Scan** 또는 **스캔 시작** 버튼 클릭
3. **"CEI-Sensor"** 장치 찾기
4. 장치 클릭하여 연결

### 단계 4: 서비스 확인

연결 후 다음 UUID를 찾아야 합니다:

```
Service UUID:
4fafc201-1fb5-459e-8fcc-c5c9c331914b
  └─ Characteristic UUID:
     beb5483e-36e1-4688-b7f5-ea07361b26ab (센서 데이터)
```

### 단계 5: 데이터 수신

1. Characteristic 클릭
2. **"Notify" 또는 "Listen for notifications"** 활성화
3. 10초마다 JSON 데이터 수신:
   ```json
   {"temperature": 22.5, "humidity": 55.2, "cei": 42.3}
   ```

---

## ❌ 여전히 검색이 안 된다면?

### 확인 사항

1. **ESP32 재부팅**
   - 업로드 후 자동으로 재부팅되지만, 수동으로 RESET 버튼 누르기

2. **시리얼 로그 확인**
   ```bash
   pio device monitor
   ```
   - "✅ BLE Advertising 시작" 메시지 확인
   - 에러 메시지가 있는지 확인

3. **핸드폰 Bluetooth 재시작**
   - Bluetooth OFF → ON
   - 앱 재시작

4. **거리 확인**
   - ESP32와 핸드폰을 1m 이내로 가까이

5. **다른 BLE 장치 간섭**
   - 주변의 다른 BLE 장치를 끄거나 멀리 이동

### 디버깅 명령어

```bash
# ESP32 재연결 확인
pio device list

# 펌웨어 재업로드
pio run --target upload

# 시리얼 로그 실시간 확인
pio device monitor --filter esp32_exception_decoder
```

---

## 🔧 추가 설정 (필요시)

### Advertising 간격 조정

`src/ble_server.c`에서:
```c
static esp_ble_adv_params_t adv_params = {
    .adv_int_min = 0x20,  // 기본값: 20ms
    .adv_int_max = 0x40,  // 기본값: 40ms
    // 더 빠른 검색을 원하면:
    // .adv_int_min = 0x10,
    // .adv_int_max = 0x20,
};
```

### 디바이스 이름 변경

`include/ble_server.h`에서:
```c
#define BLE_DEVICE_NAME  "CEI-Sensor"  // 원하는 이름으로 변경
```

---

## 📊 정상 동작 시 예상 로그

```
I (123) MAIN: ====================================
I (123) MAIN:   ESP32-S3 + OLED [DEMO 모드]
I (123) MAIN: ====================================
I (130) MAIN: NVS 초기화 중...
I (135) MAIN: ✅ NVS 초기화 완료!
I (140) MAIN: I2C 버스 초기화 중...
I (145) MAIN: ✅ I2C 버스 초기화 완료!
I (150) MAIN: ✅ OLED 디스플레이 초기화 완료!
I (2155) MAIN: BLE 서버 초기화 중...
I (2160) BLE_SERVER: BLE 서버 초기화 중...
I (2380) BLE_SERVER: ✅ BLE 서버 초기화 완료!
I (2385) BLE_SERVER: 📱 앱에서 사용할 UUID:
I (2390) BLE_SERVER:    Service UUID: 4fafc201-1fb5-459e-8fcc-c5c9c331914b
I (2395) BLE_SERVER:    Data UUID:    beb5483e-36e1-4688-b7f5-ea07361b26ab
I (2400) BLE_SERVER:    디바이스 이름: CEI-Sensor
I (2450) BLE_SERVER: ✅ BLE Advertising 시작 (이름: CEI-Sensor)
I (2455) MAIN: 온습도 측정 시작...
```

---

## ✅ 체크리스트

- [ ] ESP32에 펌웨어 업로드 완료
- [ ] 시리얼 로그에서 "NVS 초기화 완료" 확인
- [ ] 시리얼 로그에서 "BLE Advertising 시작" 확인
- [ ] 핸드폰 Bluetooth 활성화
- [ ] BLE 스캐너 앱 설치
- [ ] "CEI-Sensor" 장치 검색됨
- [ ] 장치에 연결 성공
- [ ] Service UUID 확인
- [ ] Notify 활성화
- [ ] 10초마다 데이터 수신 확인

---

## 🎉 성공!

모든 항목이 체크되었다면 BLE가 정상적으로 작동하고 있습니다!

**다음 단계:**
1. 실제 SHT-45 센서 연결
2. `#define USE_DUMMY_SENSOR 0` 으로 변경
3. 실제 센서 데이터로 테스트

---

**작성일**: 2024-11-22
**펌웨어 버전**: v1.0 (NVS 초기화 추가)

