#!/usr/bin/env python3
import sys
import time
import serial

def main():
    port = sys.argv[1] if len(sys.argv) > 1 else "/dev/ttyUSB0"
    duration = float(sys.argv[2]) if len(sys.argv) > 2 else 3.0
    baud = 115200

    try:
        ser = serial.Serial(port, baud, timeout=0.2)
    except Exception as e:
        print(f"Error opening {port}: {e}")
        sys.exit(1)

    t0 = time.time()
    try:
        while time.time() - t0 < duration:
            line = ser.readline()
            if line:
                print(line.decode("latin1", errors="replace").strip())
    except KeyboardInterrupt:
        pass
    finally:
        ser.close()

if __name__ == "__main__":
    main()
