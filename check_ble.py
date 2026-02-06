#!/usr/bin/env python3
"""
macOS에서 BLE 디바이스 스캔
"""
import asyncio
import sys

try:
    from bleak import BleakScanner
    
    async def scan_ble():
        print("🔍 BLE 디바이스 스캔 시작 (10초)...")
        print("=" * 60)
        
        devices = await BleakScanner.discover(timeout=10.0, return_adv=True)
        
        cei_found = False
        
        for address, (device, adv_data) in devices.items():
            name = device.name or "Unknown"
            
            # CEI 디바이스 찾기
            if "CEI" in name.upper():
                print(f"✅ CEI 디바이스 발견!")
                print(f"   이름: {name}")
                print(f"   주소: {address}")
                print(f"   RSSI: {adv_data.rssi} dBm")
                print(f"   Service UUIDs: {adv_data.service_uuids}")
                print()
                cei_found = True
            
            # 모든 디바이스 출력 (디버깅용)
            print(f"📱 {name} ({address}) - RSSI: {adv_data.rssi} dBm")
        
        print("=" * 60)
        print(f"총 {len(devices)}개 BLE 디바이스 발견")
        
        if not cei_found:
            print("\n❌ CEI 디바이스를 찾을 수 없습니다!")
            print("\n문제 해결:")
            print("1. ESP32-S3가 전원이 켜져 있는지 확인")
            print("2. 시리얼 로그에서 'BLE Advertising started' 확인")
            print("3. ESP32-S3를 재시작해보세요")
        
        return cei_found
    
    if __name__ == "__main__":
        found = asyncio.run(scan_ble())
        sys.exit(0 if found else 1)

except ImportError:
    print("❌ bleak 라이브러리가 설치되지 않았습니다!")
    print("\n해결 방법:")
    print("1. LightBlue 앱 사용 (App Store에서 무료 다운로드)")
    print("2. 스마트폰에서 nRF Connect 앱 사용")
    print("\n또는 bleak 설치:")
    print("   python3 -m pip install --user bleak")
    sys.exit(1)

