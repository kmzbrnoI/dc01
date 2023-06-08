#!/usr/bin/env python3
# run with unbuffered output: ‹python3 -u read.py /dev/ttyACM0›

import serial
import sys
import datetime


assert len(sys.argv) >= 2

ser = serial.Serial(port=sys.argv[1])

while True:
    chars = ser.read(1)
    if not chars:
        continue
    if chars[0] == 0x37:
        print()
        print(f'[{datetime.datetime.now().time()}]', end=' ')
        ser.read(2)  # ignore 2 characters
        continue
    print(hex(chars[0]), end=' ')
