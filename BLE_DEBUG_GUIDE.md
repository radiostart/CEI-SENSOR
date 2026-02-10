# 🔍 BLE 디바이스 검색 문제 해결 가이드

## 📱 현재 상황
- ESP32-S3 펌웨어가 성공적으로 업로드됨
- 맥북에서 CEI-Sensor 디바이스를 찾을 수 없음

## ✅ 확인 단계

### 1️⃣ ESP32-S3 재시작
ESP32-S3를 물리적으로 재시작해주세요:
- **방법 1**: USB 케이블을 뽑았다가 다시 연결
- **방법 2**: 보드의 RESET 버튼 누르기 (있는 경우)
- **방법 3**: 전원을 껐다가 다시 켜기

### 2️⃣ 시리얼 로그 확인
터미널에서 다음 명령어를 실행하여 로그를 확인하세요:

```bash
cd /Users/jay-p/Projects/CEI-SENSOR
pio device monitor --port /dev/cu.usbmodem5ABA0188561 --baud 115200
```

**확인할 메시지:**
- ✅ `NVS Flash 초기화 완료!`
- ✅ `BLE 서버 초기화 중...`
- ✅ `Bluetooth 컨트롤러 초기화 성공`
- ✅ `Bluedroid 활성화 성공`
- ✅ `BLE Advertising 시작`
- ⚠️  에러 메시지가 있는지 확인

**주의**: 만약 로그가 계속 깨져서 나온다면 보드레이트 문제일 수 있습니다.

### 3️⃣ 맥북 블루투스 설정에서 수동 검색
1. **시스템 설정** 열기
2. **Bluetooth** 클릭
3. "주변 기기" 목록에서 **CEI-Sensor** 찾기
4. 몇 초 기다려 보기 (광고 인터벌: 100-200ms)

### 4️⃣ 터미널에서 블루투스 스캔
```bash
# blueutil 설치 (아직 설치하지 않았다면)
brew install blueutil

# 블루투스 전원 확인
blueutil --power

# 블루투스 켜기 (꺼져 있다면)
blueutil --power 1

# Python BLE 스캐너 실행 (bleak 설치 후)
pip3 install bleak
python3 ble_scan.py
```

## 🐛 문제 해결

### 문제 1: 시리얼 로그가 깨져서 나옴
**원인**: 보드레이트 불일치 또는 ESP32-S3 부팅 중
**해결**: 
- ESP32-S3 재시작 후 다시 시도
- 보드레이트를 다른 값으로 시도 (9600, 57600, 115200)

### 문제 2: BLE 초기화 실패 로그
**확인할 로그:**
```
E (xxx) BLE_SERVER: Bluetooth 컨트롤러 초기화 실패: ...
E (xxx) BLE_SERVER: Bluedroid 활성화 실패: ...
```

**해결**:
1. `sdkconfig.defaults` 파일 확인:
   ```bash
   cat sdkconfig.defaults
   ```
   
   다음 설정이 있는지 확인:
   ```
   CONFIG_BT_ENABLED=y
   CONFIG_BT_BLUEDROID_ENABLED=y
   CONFIG_BT_BLE_ENABLED=y
   CONFIG_BT_BLE_42_FEATURES_SUPPORTED=y
   ```

2. 펌웨어 재빌드 및 업로드:
   ```bash
   pio run --target upload
   ```

### 문제 3: 맥북이 BLE 디바이스를 찾지 못함
**원인**: 
- ESP32-S3가 광고를 시작하지 않았을 수 있음
- 맥북 블루투스 설정 문제
- BLE 광고 파라미터 문제

**해결**:
1. **ESP32-S3 시리얼 로그 확인**
   - "BLE Advertising 시작" 메시지가 있는지 확인
   - GAP 이벤트 로그 확인

2. **맥북 블루투스 재시작**
   ```bash
   sudo pkill bluetoothd
   # 또는 시스템 설정 > Bluetooth > 켜기/끄기
   ```

3. **다른 BLE 스캐너 사용**
   - iOS/Android의 "nRF Connect" 앱 사용
   - 다른 맥북/PC에서 검색 시도

### 문제 4: `pio device monitor` 실행 실패
**오류**: `termios.error: (19, 'Operation not supported by device')`

**해결**: 
1. **screen 사용**:
   ```bash
   screen /dev/cu.usbmodem5ABA0188561 115200
   # 종료: Ctrl+A, K
   ```

2. **cu 사용**:
   ```bash
   cu -l /dev/cu.usbmodem5ABA0188561 -s 115200
   # 종료: ~.
   ```

3. **minicom 설치 및 사용**:
   ```bash
   brew install minicom
   minicom -D /dev/cu.usbmodem5ABA0188561 -b 115200
   ```

## 📊 예상 정상 로그
```
====================================
  CEI-SENSOR Starting...
====================================
I (xxx) MAIN: Initializing NVS...
I (xxx) POWER_MGR: Power manager initialized
I (xxx) SENSOR_SVC: Sensor service initialized
I (xxx) DISPLAY_SVC: Display service initialized
I (xxx) MAIN: Initializing BLE...
I (xxx) BLE_SERVER: BLE 서버 초기화 중...
I (xxx) BLE_SERVER: Bluetooth 컨트롤러 초기화 성공
I (xxx) BLE_SERVER: Bluetooth 컨트롤러 활성화 성공
I (xxx) BLE_SERVER: Bluedroid 초기화 성공
I (xxx) BLE_SERVER: Bluedroid 활성화 성공
I (xxx) BLE_SERVER: GATTS 콜백 등록 성공
I (xxx) BLE_SERVER: GAP 콜백 등록 성공
I (xxx) BLE_SERVER: GATTS 앱 등록 시작
I (xxx) BLE_SERVER: BLE 디바이스 이름 설정: CEI-Sensor
I (xxx) BLE_SERVER: Advertising 데이터 설정 완료
I (xxx) BLE_SERVER: Scan response 데이터 설정 완료
I (xxx) BLE_SERVER: ✅ BLE Advertising 시작!
I (xxx) MAIN: 온습도 측정 시작...
```

## 🔄 다음 단계
1. ESP32-S3 재시작
2. 시리얼 로그 확인 (위 방법 중 하나 사용)
3. 로그 내용을 확인하여 BLE 초기화 성공 메시지 찾기
4. 맥북 블루투스 설정에서 CEI-Sensor 검색
5. 문제가 계속되면 로그 전체를 확인하여 에러 메시지 찾기

## 📝 참고
- **BLE 디바이스 이름**: `CEI-Sensor`
- **Service UUID**: `4fafc201-1fb5-459e-8fcc-c5c9c331914b`
- **광고 인터벌**: 100-200ms (0xA0-0x140)
- **광고 타입**: `ADV_TYPE_IND` (일반 검색 가능)

---

**💡 팁**: ESP32-S3를 재시작한 후 최소 5-10초 기다려 주세요. BLE 초기화와 광고 시작에 시간이 걸릴 수 있습니다.

