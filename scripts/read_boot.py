#!/usr/bin/env python3
"""Open the ESP32-S3 USB-CDC, reset the chip via the USB-Serial/JTAG RTS line,
and capture boot output. Usage: python3 read_boot.py <port> [seconds]"""
import serial
import sys
import time

port = sys.argv[1] if len(sys.argv) > 1 else "/dev/cu.usbmodem14301"
seconds = float(sys.argv[2]) if len(sys.argv) > 2 else 12

ser = serial.Serial(port, 115200, timeout=0.2)
time.sleep(0.5)
# Flush any stale data.
ser.reset_input_buffer()

# Reset the chip: USB-Serial/JTAG maps RTS -> EN (active low).
ser.setRTS(True)
time.sleep(0.1)
ser.setRTS(False)  # assert reset
time.sleep(0.15)
ser.setRTS(True)   # release reset -> boots the app

data = b""
end = time.time() + seconds
while time.time() < end:
    chunk = ser.read(4096)
    if chunk:
        data += chunk
try:
    sys.stdout.buffer.write(data)
except Exception:
    sys.stdout.write(data.decode(errors="replace"))
sys.stdout.flush()
ser.close()
