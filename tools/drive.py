#!/usr/bin/env python3
"""Script the serial pad: send keys and print the log. Steps are 'keys' or 'sleep:N' or 'log:N' (print N s of log).
Usage: tools/drive.py step [step...]   e.g. tools/drive.py dk log:12 m s k log:5"""
import serial, sys, glob, time, re
port = glob.glob('/dev/cu.usbmodem*')[0]
s = serial.Serial(port, 115200, timeout=0.2)
ansi = re.compile(r'\x1b\[[0-9;]*m')
def log(secs):
    t = time.time()
    while time.time() - t < secs:
        d = s.read(4096)
        if d:
            for line in ansi.sub('', d.decode('utf8', 'replace')).splitlines():
                if re.search(r'SFES|CORE|SAVES|MEDAL|MUSIC|BLE|abort|assert|Guru|Backtrace', line):
                    print(re.sub(r'^[IWE] \(\d+\) ', '', line), flush=True)
for step in sys.argv[1:]:
    if step.startswith('sleep:'): time.sleep(float(step[6:]))
    elif step.startswith('log:'): log(float(step[4:]))
    else:
        for k in step:
            s.write(k.encode()); s.flush(); time.sleep(0.2)
