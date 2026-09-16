#!/usr/bin/env python3
"""Type keys into the board's serial console (they act as the pad; 'n' = next ROM). Usage: tools/keys.py <keys> [port]"""
import serial, sys, glob, time
keys = sys.argv[1]
port = sys.argv[2] if len(sys.argv) > 2 else glob.glob('/dev/cu.usbmodem*')[0]
s = serial.Serial(port, 115200, timeout=0.2)
for k in keys:
    s.write(k.encode()); s.flush(); time.sleep(0.15)
