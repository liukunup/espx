#!/usr/bin/env python3
"""
ESPX device test harness (serial console).

Drives the manufacturing test-mode console over UART and asserts on the output.
Used for both interactive bring-up and CI-style regression checks.

Usage:
    tools/espx_test.py --port /dev/cu.usbserial-A5069RR4 enter
    tools/espx_test.py --port /dev/cu.usbserial-A5069RR4 selftest
    tools/espx_test.py --port /dev/cu.usbserial-A5069RR4 all

Subcommands:
    info      print chip / partition / boot info from the boot log
    enter     reboot into test mode (long-press BOOT over the auto-reset lines)
    selftest  add sample devices, run "test all", assert the report
    console   interactive passthrough
    all       info + enter + selftest

Exit code is non-zero when an assertion fails, so this is usable as a gate.
"""

import argparse
import re
import sys
import time

try:
    import serial
except ImportError:
    sys.exit("pyserial is required:  pip install pyserial")

BAUD = 115200


class Console:
    def __init__(self, port, baud=BAUD, echo=False):
        # dsrdtr/rtscts must be off, otherwise the adapter asserts DTR/RTS on
        # open and holds the chip in reset (or drives the line into a break).
        self.s = serial.Serial(port, baud, timeout=0.2,
                               dsrdtr=False, rtscts=False)
        self.s.dtr = False
        self.s.rts = False
        time.sleep(0.2)
        self.s.reset_input_buffer()
        self.echo = echo
        self.log = []

    def reset(self, boot_delay=2.5):
        """Pulse EN (RTS) to reboot the chip, keeping GPIO0 (DTR) high."""
        s = self.s
        s.dtr = False          # GPIO0 high -> normal boot
        s.rts = False
        time.sleep(0.1)
        s.rts = True           # EN low  -> reset asserted
        time.sleep(0.15)
        s.rts = False          # EN high -> run
        time.sleep(0.05)
        s.reset_input_buffer()
        time.sleep(boot_delay)

    def read(self, seconds):
        out = []
        end = time.time() + seconds
        while time.time() < end:
            chunk = self.s.read(8192)
            if chunk:
                out.append(chunk)
        data = b"".join(out)
        text = data.decode("utf-8", "replace").replace("\r", "")
        self.log.append(text)
        if self.echo:
            sys.stdout.write(text)
            sys.stdout.flush()
        return text

    def send(self, line, wait=2.0):
        self.s.write((line + "\n").encode())
        return self.read(wait)

    def hold_boot(self, seconds=3.5):
        """Hold GPIO0 low while the app is running (auto-reset circuit)."""
        self.s.dtr = True
        self.read(seconds)
        self.s.dtr = False

    def close(self):
        self.s.close()


def expect(text, needle, label):
    ok = needle in text
    print(f"  [{'PASS' if ok else 'FAIL'}] {label}")
    return ok


def cmd_info(args):
    c = Console(args.port)
    try:
        return _info(c)
    finally:
        c.close()


def _info(c):
    c.reset()
    log = c.read(6)

    print("Boot information")
    ok = True
    ok &= expect(log, "ESP-ROM:esp32s3", "chip is ESP32-S3")
    ok &= expect(log, "SPI Flash Size : 16MB", "flash detected as 16MB")
    ok &= expect(log, "Found 8MB PSRAM device", "8MB PSRAM detected")
    ok &= expect(log, "otadata", "partition table has otadata")
    ok &= expect(log, "factory", "partition table has factory slot")
    ok &= expect(log, "ESPX IoT Device Firmware", "application banner")
    return 0 if ok else 1


def enter_test_mode(c, timeout=25):
    """Reboot the device into test mode; returns the console log."""
    c.reset(boot_delay=3.0)
    log = c.read(2)
    c.hold_boot(3.5)
    log += c.read(8)

    if "MANUFACTURING TEST MODE" not in log:
        # maybe already there
        log += c.send("help", 2)

    return log


def cmd_enter(args):
    c = Console(args.port)
    try:
        return _enter(c)
    finally:
        c.close()


def _enter(c):
    log = enter_test_mode(c)

    print("Entering test mode")
    ok = expect(log, "MANUFACTURING TEST MODE", "test-mode banner")
    ok &= expect(log, "espx-test>", "console prompt")
    return 0 if ok else 1


DEVICES = [
    ('ws_led', 'ws2812',       '{"din":48,"count":1}'),
    ('relay1', 'relay',        '{"gpio":5,"active_level":1}'),
    ('sr1',    'shiftreg_595', '{"din":16,"clock_gpio":15,"latch_gpio":7,"count":2}'),
    ('btn1',   'button',       '{"gpio":9,"active_level":0}'),
]


def cmd_selftest(args):
    c = Console(args.port)
    try:
        return _selftest(c)
    finally:
        c.close()


def _selftest(c):
    log = enter_test_mode(c)
    if "MANUFACTURING TEST MODE" not in log:
        print("could not enter test mode")
        return 1

    ok = True

    print("Device registry")
    log = c.send("reset", 2.0)
    for did, dtype, cfg in DEVICES:
        log = c.send(f"add {did} {dtype} {cfg}", 2.0)
        ok &= expect(log, f"Added '{did}'", f"add {did} ({dtype})")

    log = c.send("list", 2.0)
    for did, _, _ in DEVICES:
        ok &= expect(log, did, f"listed {did}")

    print("Hardware self-test")
    log = c.send("test all", 20.0)
    ok &= expect(log, "Running hardware self-test", "self-test started")

    for did, dtype, _ in DEVICES:
        line = next((l for l in log.split("\n") if f"[TEST] {did}" in l), "")
        verdict = "PASS" if "PASS" in line else ("FAIL" if "FAIL" in line else "?")
        print(f"  {did:<10} {dtype:<14} -> {verdict}")

    ok &= expect(log, "passed", "self-test summary printed")

    # Reboot back to the application
    c.send("exit", 1.0)
    return 0 if ok else 1


def cmd_console(args):
    c = Console(args.port, echo=True)
    print("--- interactive; Ctrl-C to quit ---")
    try:
        while True:
            c.read(0.5)
    except KeyboardInterrupt:
        pass
    finally:
        c.close()
    return 0


def cmd_all(args):
    c = Console(args.port)
    try:
        rc = _info(c)
        rc |= _enter(c)
        rc |= _selftest(c)
    finally:
        c.close()

    print()
    print("RESULT:", "PASS" if rc == 0 else "FAIL")
    return rc


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", required=True, help="serial port")
    ap.add_argument("cmd", choices=["info", "enter", "selftest", "console", "all"])
    args = ap.parse_args()

    return {"info": cmd_info, "enter": cmd_enter, "selftest": cmd_selftest,
            "console": cmd_console, "all": cmd_all}[args.cmd](args)


if __name__ == "__main__":
    sys.exit(main())
