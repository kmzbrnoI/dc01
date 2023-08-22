#!/usr/bin/env python3

"""
DC-01 watchdog for hJOP

Usage:
  watchdog.py [options]
  watchdog.py --version

Options:
  -s <servername>    hJOPserver address [default: localhost]
  -p <port>          hJOPserver PT server port [default: 5896]
  -c <port>          DC-01 serial port
  -l <loglevel>      Specify loglevel (python logging package) [default: info]
  -h --help          Show this screen.
  --version          Show version.
"""

import sys
from docopt import docopt
import logging
from typing import List, Tuple
import serial
import serial.tools.list_ports
import select
import datetime


APP_VERSION = '1.0'

DC01_DESCRIPTION = 'DC-01'
DC01_BAUDRATE = 115200
REFRESH_PERIOD = 0.2  # seconds
DC01_RECEIVE_TIMEOUT = datetime.timedelta(milliseconds=200)
DC01_RECEIVE_MAGIC = [0x37, 0xE2]

DC_CMD_PM_INFO_REQ = 0x10
DC_CMD_PM_SET_STATE = 0x11
DC_CMD_PM_PING = 0x02

DC_CMD_MP_INFO = 0x10
DC_CMD_MP_STATE = 0x11
DC_CMD_MP_BRSTATE = 0x12

DC01_MODE = ['mInitializing', 'mNormalOp', 'mOverride', 'mFailure']


def ports() -> List[Tuple[str, str]]:
    return [(port.device, port.product) for port in serial.tools.list_ports.comports()]


def dc01_ports() -> List[str]:
    return [device for device, product in ports() if product == DC01_DESCRIPTION]


def dc01_send(data: List[int], port) -> None:
    to_send = [0x37, 0xE2, len(data)] + data
    logging.debug(f'< Send: {to_send}')
    port.write(to_send)


def dc01_send_relay(state, port) -> None:
    dc01_send([DC_CMD_PM_SET_STATE, int(state)], port)


def dc01_parse(data: List[int]) -> None:
    logging.debug(f'> Received: {data}')
    useful_data = data[3:]

    if not useful_data:
        return

    if useful_data[0] == DC_CMD_MP_STATE and len(useful_data) >= 4:
        mode = (useful_data[1] >> 4)
        dcc_connected = bool(useful_data[1] & 1)
        dcc_at_least_one = bool((useful_data[1] >> 1) & 1)
        failure_code = useful_data[2]
        warnings = useful_data[3]

        logging.info(f'Received: mode={DC01_MODE[mode]}, {dcc_connected=}, {dcc_at_least_one=}, {failure_code=}, {warnings=}')


def hjopserver_ok() -> bool:
    return True


def main() -> None:
    args = docopt(__doc__, version=APP_VERSION)

    loglevel = {
        'debug': logging.DEBUG,
        'info': logging.INFO,
        'warning': logging.WARNING,
        'error': logging.ERROR,
        'critical': logging.CRITICAL,
    }.get(args['-l'], logging.INFO)
    logging.basicConfig(
        level=loglevel,
        format='[%(asctime)s.%(msecs)03d] %(levelname)s %(message)s',
        datefmt='%Y-%m-%d %H:%M:%S',
    )

    ports = dc01_ports()
    if len(ports) < 1:
        sys.stderr.write('No DC-01 found!\n')
        sys.exit(1)
    if len(ports) > 1 and args['-c'] == '':
        sys.stderr.write('Multiple DC-01s found!\n')
        sys.exit(1)

    ser = serial.Serial(port=ports[0], baudrate=DC01_BAUDRATE, timeout=0)

    receive_buf = []
    next_poll = datetime.datetime.now()
    last_receive_time = datetime.datetime.now()
    while True:
        read, _, _ = select.select([ser], [], [], REFRESH_PERIOD)
        received = ser.read(0x100)

        if datetime.datetime.now() > next_poll:
            next_poll = datetime.datetime.now() + datetime.timedelta(seconds=REFRESH_PERIOD)
            dc01_send_relay(hjopserver_ok(), ser)

        if received:
            if receive_buf and datetime.datetime.now()-last_receive_time > DC01_RECEIVE_TIMEOUT:
                logging.debug('Clearing data, timeout!')
                receive_buf.clear()
            last_receive_time = datetime.datetime.now()
            receive_buf += received
            while len(receive_buf) >= len(DC01_RECEIVE_MAGIC) and receive_buf[0:len(DC01_RECEIVE_MAGIC)] != DC01_RECEIVE_MAGIC:
                logging.debug(f'Popping packet: {receive_buf[0]}')
                receive_buf.pop(0)

            while len(receive_buf) >= 3 and len(receive_buf) >= receive_buf[2]+3:
                packet_length = receive_buf[len(DC01_RECEIVE_MAGIC)]+3
                dc01_parse(receive_buf[0:packet_length])
                receive_buf = receive_buf[packet_length:]
                while len(receive_buf) >= len(DC01_RECEIVE_MAGIC) and receive_buf[0:len(DC01_RECEIVE_MAGIC)] != DC01_RECEIVE_MAGIC:
                    logging.debug(f'Popping packet: {receive_buf[0]}')
                    receive_buf.pop(0)


if __name__ == '__main__':
    main()
