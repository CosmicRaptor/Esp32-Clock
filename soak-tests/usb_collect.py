#!/usr/bin/env python3
"""Record USB serial locally for 24 hours; requests read-only memstats once a minute."""
import datetime as dt
import glob
import json
import os
from pathlib import Path
import select
import termios
import time

folder = Path(__file__).resolve().parent / ('usb-' + dt.datetime.now().strftime('%Y%m%d-%H%M%S'))
folder.mkdir()
end = time.monotonic() + 86400
print(str(folder), flush=True)
with (folder / 'serial.jsonl').open('a', buffering=1) as out:
    def log(kind, text):
        out.write(json.dumps({'at': dt.datetime.now(dt.timezone.utc).isoformat(), kind: text}) + '\n')
    while time.monotonic() < end:
        fd = None
        try:
            ports = glob.glob('/dev/cu.usbmodem*')
            if len(ports) != 1:
                raise OSError('Expected one USB board; found ' + str(len(ports)))
            fd = os.open(ports[0], os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
            attrs = termios.tcgetattr(fd)
            attrs[0] = attrs[1] = attrs[3] = 0
            attrs[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
            attrs[4] = attrs[5] = termios.B115200
            attrs[6][termios.VMIN] = 0
            attrs[6][termios.VTIME] = 0
            termios.tcsetattr(fd, termios.TCSANOW, attrs)
            log('event', 'connected ' + ports[0])
            pending = b''
            next_sample = 0
            while time.monotonic() < end:
                if time.monotonic() >= next_sample:
                    os.write(fd, b'memstats\n')
                    next_sample = time.monotonic() + 60
                if not select.select([fd], [], [], 1)[0]:
                    continue
                data = os.read(fd, 8192)
                if not data:
                    # macOS VMIN=0 can return an empty read on a live serial port.
                    # Reopening on that condition creates false disconnects and
                    # resets the sampling schedule. Real removals/errors reconnect.
                    if not os.path.exists(ports[0]):
                        raise OSError('USB disconnected')
                    time.sleep(0.05)
                    continue
                pending += data
                while b'\n' in pending:
                    line, pending = pending.split(b'\n', 1)
                    log('serial', line.decode(errors='replace').rstrip('\r'))
                if len(pending) > 16384:
                    log('serial_partial', pending.decode(errors='replace'))
                    pending = b''
            next_sample = 0
        except OSError as error:
            log('event', str(error))
            time.sleep(5)
        finally:
            if fd is not None:
                os.close(fd)
    log('event', '24-hour capture complete')
