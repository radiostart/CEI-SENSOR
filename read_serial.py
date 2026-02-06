#!/usr/bin/env python3
"""
ESP32-S3 시리얼 로그 읽기
"""
import serial
import time
import sys

SERIAL_PORT = "/dev/cu.usbmodem5ABA0188561"
BAUD_RATE = 115200
READ_DURATION = 10  # 10초간 읽기

def main():
    print(f"📡 시리얼 포트 연결: {SERIAL_PORT} @ {BAUD_RATE}")
    print(f"⏱️  {READ_DURATION}초간 로그 읽기...")
    print("=" * 70)
    
    try:
        ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=1)
        time.sleep(0.5)  # 연결 안정화
        
        # 버퍼 클리어
        ser.reset_input_buffer()
        
        start_time = time.time()
        line_buffer = ""
        
        while (time.time() - start_time) < READ_DURATION:
            if ser.in_waiting > 0:
                try:
                    chunk = ser.read(ser.in_waiting).decode('utf-8', errors='replace')
                    print(chunk, end='', flush=True)
                except Exception as e:
                    print(f"\n⚠️  디코딩 오류: {e}", file=sys.stderr)
            else:
                time.sleep(0.1)
        
        print()
        print("=" * 70)
        print("✅ 로그 읽기 완료")
        
        ser.close()
        
    except serial.SerialException as e:
        print(f"❌ 시리얼 포트 오류: {e}", file=sys.stderr)
        sys.exit(1)
    except KeyboardInterrupt:
        print("\n\n⚠️  사용자가 중단했습니다.")
        sys.exit(0)
    except Exception as e:
        print(f"❌ 예상치 못한 오류: {e}", file=sys.stderr)
        sys.exit(1)

if __name__ == "__main__":
    main()

