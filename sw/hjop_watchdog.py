#!/usr/bin/env python3

"""
DC-01 watchdog for hJOP

Usage:
  watchdog.py [options]
  watchdog.py --help
  watchdog.py --version

Options:
  -s <servername>    hJOPserver address [default: 127.0.0.1]
  -p <port>          hJOPserver PT server port [default: 5823]
  -c <port>          DC-01 serial port
  -l <loglevel>      Specify loglevel (python logging package) [default: info]
  -m --mock          Mock server - keep output always on
  -h --help          Show this screen
  -v --version       Show version
  -r --resume        Always try to resume operations, never die (suitable for production deployment)
  --nocolor          Do not print colors to terminal
  -d <dir>           Set logging directory to <dir>
"""

import os
import sys
from docopt import docopt
import logging
from typing import List, Tuple, Dict, Any
import serial
import datetime
import urllib.request
import urllib.error
import json
import time
import socket

if os.name == 'nt':
    import list_ports_windows as list_ports
    import colorama
else:
    import serial.tools.list_ports as list_ports


APP_VERSION = '1.0'

DC01_DESCRIPTION = 'DC-01'
DC01_BAUDRATE = 115200
REFRESH_PERIOD = 0.25  # seconds
WHILE_PERIOD = REFRESH_PERIOD/5
DC01_RECEIVE_TIMEOUT = datetime.timedelta(milliseconds=3*WHILE_PERIOD)
DC01_RECEIVE_MAGIC = [0x37, 0xE2]
DC01_SEND_MAGIC = [0x37, 0xE2]
DC01_OK_VERSIONS = ['1.0']

DC_CMD_PM_INFO_REQ = 0x10
DC_CMD_PM_SET_STATE = 0x11
DC_CMD_PM_PING = 0x02

DC_CMD_MP_INFO = 0x10
DC_CMD_MP_STATE = 0x11
DC_CMD_MP_BRSTATE = 0x12

DC01_MODE = ['mInitializing', 'mNormalOp', 'mOverride', 'mFailure']


###############################################################################
# Common

class ColorFormatter(logging.Formatter):
    GREY = '\x1b[38;20m'
    YELLOW = '\x1b[1;33;20m'
    RED = '\x1b[1;31;20m'
    RESET = '\x1b[0m'

    LEVEL_COLORS = {
        logging.WARNING: YELLOW,
        logging.ERROR: RED,
        logging.CRITICAL: RED,
    }

    def format(self, record):
        log_fmt = self.LEVEL_COLORS.get(record.levelno, self.GREY) + self._fmt + self.RESET
        formatter = logging.Formatter(log_fmt)
        return formatter.format(record)


def supports_color() -> bool:
    """
    Returns True if the running system's terminal supports color, and False
    otherwise.
    """
    supported_platform = sys.platform != 'Pocket PC'
    # isatty is not always implemented, #6223.
    is_a_tty = hasattr(sys.stdout, 'isatty') and sys.stdout.isatty()
    return supported_platform and is_a_tty


###############################################################################
# Communication with DC-01

def ports() -> List[Tuple[str, str]]:
    return [(port.device, port.product) for port in list_ports.comports()]


def dc01_ports() -> List[str]:
    return [device for device, product in ports() if product == DC01_DESCRIPTION]


def dc01_send(data: List[int], port: serial.Serial) -> None:
    to_send = DC01_SEND_MAGIC + [len(data)] + data
    logging.debug(f'< Send: {to_send}')
    port.write(to_send)


def dc01_send_relay(state: bool, port: serial.Serial) -> None:
    dc01_send([DC_CMD_PM_SET_STATE, int(state)], port)


def dc01_brtest_state(state: int) -> str:
    match state:
        case 0: return 'not yet run'
        case 1: return 'in progress'
        case 2: return 'succesfully completed'
        case 3: return 'failed'
        case 4: return 'interrupted due to DCC absence'
        case _: return 'unknown'


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

        level = logging.INFO if failure_code == 0 and warnings == 0 and mode == 1 \
            else logging.WARNING
        logging.log(
            level,
            f'Received: mode={DC01_MODE[mode]}, {dcc_connected=}, '
            f'{dcc_at_least_one=}, {failure_code=}, {warnings=}'
        )

    elif useful_data[0] == DC_CMD_MP_INFO and len(useful_data) >= 3:
        fw_major, fw_minor = useful_data[1], useful_data[2]
        fw_version_str = f'{fw_major}.{fw_minor}'
        logging.info(f'Received: DC-01 FW=v{fw_version_str}')
        if fw_version_str not in DC01_OK_VERSIONS:
            logging.warning('DC-01 FW version is not supported (outdated version?)!')

    elif useful_data[0] == DC_CMD_MP_BRSTATE and len(useful_data) >= 4:
        state, step, error = useful_data[1:4]
        logging.info(f'Received: BRTest state: {dc01_brtest_state(state)}, {step=}, {error=}')


###############################################################################
# Communication with hJOP


def pt_get(path: str, server: str, port: int) -> Dict[str, Any]:
    logging.debug(f'PT GET {path}')
    logging.debug(f'{server=}, {port=}')
    if not path.startswith('/'):
        path = '/' + path

    req = urllib.request.Request(
        f'http://{server}:{port}{path}',
        headers={
            'Content-type': 'application/json',
        },
        method='GET',
    )
    with urllib.request.urlopen(req, timeout=REFRESH_PERIOD) as response:
        data = response.read().decode('utf-8')
    return json.loads(data)  # type: ignore


def hjopserver_ok(server: str, port: int) -> bool:
    try:
        response = pt_get('/status', server, port)
        emergency = response['trakce']['emergency']
        logging.info('hJOP EMERGENCY' if emergency else 'hJOP OK')
        return not emergency
    except (urllib.error.URLError, urllib.error.HTTPError, socket.error) as e:
        logging.info(f'Unable to read hJOPserver status: {e}')
        return False


###############################################################################
# main

def run(dc01_port: str, args) -> None:
    logging.info(f'Connecting to {dc01_port}...')
    ser = serial.Serial(port=dc01_port, baudrate=DC01_BAUDRATE, timeout=0)
    dc01_send([DC_CMD_PM_INFO_REQ], ser)  # Get DC-01 info

    receive_buf: List[int] = []
    next_poll = datetime.datetime.now()
    last_receive_time = datetime.datetime.now()
    while True:
        received = ser.read(0x100)  # timeout=0 = opened in non-blocking mode

        if datetime.datetime.now() >= next_poll:
            next_poll = datetime.datetime.now() + datetime.timedelta(seconds=REFRESH_PERIOD)
            if args['--mock'] or hjopserver_ok(args['-s'], int(args['-p'])):
                dc01_send_relay(True, ser)

        if received:
            if receive_buf and datetime.datetime.now()-last_receive_time > DC01_RECEIVE_TIMEOUT:
                logging.debug('Clearing data, timeout!')
                receive_buf.clear()
            last_receive_time = datetime.datetime.now()
            receive_buf += received
            while (len(receive_buf) >= len(DC01_RECEIVE_MAGIC) and
                   receive_buf[0:len(DC01_RECEIVE_MAGIC)] != DC01_RECEIVE_MAGIC):
                logging.debug(f'Popping packet: {receive_buf[0]}')
                receive_buf.pop(0)

            while len(receive_buf) >= 3 and len(receive_buf) >= receive_buf[2]+3:
                packet_length = receive_buf[len(DC01_RECEIVE_MAGIC)]+3
                dc01_parse(receive_buf[0:packet_length])
                receive_buf = receive_buf[packet_length:]
                while (len(receive_buf) >= len(DC01_RECEIVE_MAGIC) and
                       receive_buf[0:len(DC01_RECEIVE_MAGIC)] != DC01_RECEIVE_MAGIC):
                    logging.debug(f'Popping packet: {receive_buf[0]}')
                    receive_buf.pop(0)

        time.sleep(WHILE_PERIOD)


def main() -> None:
    args = docopt(__doc__, version=APP_VERSION)

    logformat = '[%(asctime)s] %(levelname)s %(message)s'
    loglevel = {
        'debug': logging.DEBUG,
        'info': logging.INFO,
        'warning': logging.WARNING,
        'error': logging.ERROR,
        'critical': logging.CRITICAL,
    }.get(args['-l'], logging.INFO)
    logging.getLogger().setLevel(loglevel)

    if os.name == 'nt':
        colorama.just_fix_windows_console()

    # Replace default logging terminal handler with ColorFormatter handler
    streamHandler = logging.StreamHandler(stream=sys.stdout)
    color = not args['--nocolor'] and supports_color()
    print(color)
    formatter = ColorFormatter if color else logging.Formatter
    streamHandler.setFormatter(formatter(logformat))
    logging.getLogger().addHandler(streamHandler)

    if args['-d']:
        # Add file handler
        if not os.path.exists(args['-d']):
            os.makedirs(args['-d'])
        filename = os.path.join(args['-d'], datetime.datetime.now().strftime('%Y-%m-%d')+'.log')
        fileHandler = logging.FileHandler(filename)
        fileHandler.setFormatter(logging.Formatter(logformat))
        logging.getLogger().addHandler(fileHandler)

    while True:
        if not args['-c']:
            logging.info('Looking for DC-01...')
        _ports = [args['-c']] if args['-c'] else dc01_ports()

        if len(_ports) < 1:
            logging.error('No DC-01 found!')
        elif len(_ports) > 1:
            logging.error('Multiple DC-01s found!')
        else:
            try:
                run(_ports[0], args)
            except serial.serialutil.SerialException as e:
                logging.error(f'SerialException: {e}')
            except Exception as e:
                if args['-r']:
                    logging.error(f'Exception: {e}')
                else:
                    raise

        time.sleep(3)  # sleep before reconnect


if __name__ == '__main__':
    main()
