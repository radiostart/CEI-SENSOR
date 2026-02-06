#!/usr/bin/env python3
"""
간단한 BLE 스캐너
macOS에서 CEI-Sensor 디바이스를 검색합니다.
"""
import asyncio

try:
    from bleak import BleakScanner
    BLEAK_AVAILABLE = True
except ImportError:
    BLEAK_AVAILABLE = False
    print("⚠️  bleak 라이브러리가 설치되지 않았습니다.")
    print("설치 방법: pip3 install bleak")
    print("\n대신 시스템 명령어로 블루투스 상태를 확인합니다...\n")

async def scan_ble_devices():
    """BLE 디바이스를 10초간 스캔합니다."""
    print("🔍 BLE 디바이스 스캔 시작 (10초)...")
    print("-" * 60)
    
    devices = await BleakScanner.discover(timeout=10.0, return_adv=True)
    
    found_cei_sensor = False
    
    for address, (device, adv_data) in devices.items():
        device_name = device.name or "Unknown"
        
        # 모든 디바이스 출력
        print(f"📱 {device_name}")
        print(f"   주소: {address}")
        print(f"   RSSI: {adv_data.rssi} dBm")
        
        # CEI-Sensor 찾기
        if "CEI" in device_name or "Sensor" in device_name:
            print(f"   ✅ CEI-Sensor 발견!")
            found_cei_sensor = True
        
        print()
    
    print("-" * 60)
    print(f"총 {len(devices)}개의 BLE 디바이스 발견")
    
    if found_cei_sensor:
        print("✅ CEI-Sensor를 찾았습니다!")
    else:
        print("⚠️  CEI-Sensor를 찾지 못했습니다.")
        print("\n문제 해결 방법:")
        print("1. ESP32-S3가 켜져 있는지 확인")
        print("2. 시리얼 모니터에서 'BLE Advertising' 메시지 확인")
        print("3. ESP32-S3를 재시작해보세요")

def check_bluetooth_status():
    """macOS 블루투스 상태 확인 (bleak 없이)"""
    import subprocess
    
    print("🔍 macOS 블루투스 상태 확인 중...")
    print("-" * 60)
    
    try:
        # 블루투스 전원 상태 확인
        result = subprocess.run(
            ["system_profiler", "SPBluetoothDataType"],
            capture_output=True,
            text=True,
            timeout=10
        )
        
        output = result.stdout
        
        # CEI-Sensor 검색
        if "CEI-Sensor" in output or "CEI" in output:
            print("✅ CEI-Sensor 발견!")
            # CEI-Sensor 관련 라인 출력
            for line in output.split('\n'):
                if 'CEI' in line or 'Sensor' in line:
                    print(f"   {line.strip()}")
        else:
            print("⚠️  CEI-Sensor를 찾지 못했습니다.")
            print("\n연결된 블루투스 디바이스 목록:")
            
            # 디바이스 이름 추출
            in_device_section = False
            for line in output.split('\n'):
                if 'Devices' in line:
                    in_device_section = True
                if in_device_section and ':' in line and 'Address' not in line:
                    device_info = line.strip()
                    if device_info and not device_info.startswith('Bluetooth'):
                        print(f"   📱 {device_info}")
        
        print("-" * 60)
        
    except subprocess.TimeoutExpired:
        print("⚠️  시스템 프로파일러 타임아웃")
    except Exception as e:
        print(f"⚠️  오류 발생: {e}")

def main():
    print("=" * 60)
    print("CEI-Sensor BLE 스캐너")
    print("=" * 60)
    print()
    
    if BLEAK_AVAILABLE:
        asyncio.run(scan_ble_devices())
    else:
        check_bluetooth_status()

if __name__ == "__main__":
    main()

