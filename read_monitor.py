import serial
import sys
import time

port = '/dev/cu.usbmodem101'
baud = 115200

print(f"Connecting to {port} at {baud}...")

try:
    ser = serial.Serial(port, baud, timeout=0.1)
    print(f"Connected to {ser.name}")
    
    # Clear buffer
    ser.reset_input_buffer()
    
    start_time = time.time()
    while True:
        # Read line by line
        if ser.in_waiting:
            try:
                line = ser.readline().decode('utf-8', errors='replace').rstrip()
                if line:
                    print(line)
                    sys.stdout.flush()
            except Exception as e:
                print(f"Read error: {e}")
        else:
            time.sleep(0.01)
            
        # Run for a limited time to avoid hanging the tool if called directly (though user wants monitoring)
        # Actually, for "monitoring", I should probably run it for a short burst or in background.
        # But the user said "monitor please", implying they want to see output.
        # I'll run it for 10 seconds to show a snapshot, then ask if they want more.
        if time.time() - start_time > 15: 
             print("\n[Monitoring paused after 15 seconds. Run again to continue.]")
             break

except serial.SerialException as e:
    print(f"Serial error: {e}")
except KeyboardInterrupt:
    print("\nExiting...")
except Exception as e:
    print(f"Error: {e}")

