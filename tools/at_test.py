#!/usr/bin/env python3
"""
AT command interface tests.

Drives the AT service over its UART and asserts on the ESP-AT style replies
(\\r\\nOK\\r\\n, \\r\\n+CMD:<value>\\r\\n, \\r\\nERROR\\r\\n).

Usage:
    tools/at_test.py --port /dev/cu.usbserial-0001
    tools/at_test.py --port /dev/cu.usbserial-0001 --manual

The AT UART is independent of the log/console UART, so this runs while the
application is up (see CONFIG_ESPX_AT_UART_*; default UART1, TX=GPIO4 RX=GPIO5).

Exit code is non-zero when a check fails.
"""

import argparse
import re
import sys
import time

try:
    import serial
except ImportError:
    sys.exit("pyserial is required:  pip install pyserial")


class AT:
    def __init__(self, port, baud=115200):
        # dsrdtr/rtscts off: asserting them on open resets/holds the target.
        self.s = serial.Serial(port, baud, timeout=0.3, dsrdtr=False, rtscts=False)
        self.s.dtr = False
        self.s.rts = False
        time.sleep(0.2)
        self.s.reset_input_buffer()

    def send(self, cmd, wait=1.5):
        self.s.reset_input_buffer()
        self.s.write((cmd + "\r\n").encode())
        end = time.time() + wait
        out = b""
        while time.time() < end:
            chunk = self.s.read(4096)
            if chunk:
                out += chunk
                # stop early once a final result code has arrived
                if b"\r\nOK\r\n" in out or b"\r\nERROR\r\n" in out:
                    time.sleep(0.15)
                    out += self.s.read(4096)
                    break
        return out.decode("utf-8", "replace").replace("\r", "")

    def close(self):
        self.s.close()


results = []


def check(label, ok, detail=""):
    results.append((label, ok))
    mark = "PASS" if ok else "FAIL"
    line = f"  [{mark}] {label}"
    if not ok and detail:
        line += f"  — {detail}"
    print(line)
    return ok


def ok_reply(text):
    return "\nOK\n" in text


def err_reply(text):
    return "\nERROR\n" in text


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", required=True)
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--manual", action="store_true",
                    help="interactive: type commands, see replies")
    args = ap.parse_args()

    at = AT(args.port, args.baud)

    if args.manual:
        print("--- type AT commands; Ctrl-C to quit ---")
        try:
            while True:
                line = input("> ")
                print(at.send(line))
        except (KeyboardInterrupt, EOFError):
            pass
        finally:
            at.close()
        return 0

    print("Basic protocol")
    r = at.send("AT")
    check("AT -> OK", ok_reply(r), repr(r))

    r = at.send("at")
    check("AT is case-insensitive", ok_reply(r), repr(r))

    r = at.send("AT+NOSUCHCOMMAND")
    check("unknown command -> ERROR with a hint",
          err_reply(r) and "unsupported" in r, repr(r))

    print("Identification")
    r = at.send("AT+GMR")
    check("AT+GMR reports AT/SDK/firmware versions",
          ok_reply(r) and "AT version:" in r and "Firmware version:" in r, repr(r))
    for line in r.split("\n"):
        if line.strip():
            print(f"        {line.strip()}")

    r = at.send("AT+ID")
    check("AT+ID reports the device id", ok_reply(r) and "+ID:" in r, repr(r))

    print("Query forms")
    r = at.send("AT+CFG?")
    check("AT+CFG? exports the configuration as JSON",
          ok_reply(r) and "+CFG:{" in r, repr(r))

    r = at.send("AT+DEVTYPE?")
    check("AT+DEVTYPE? lists the driver catalogue",
          ok_reply(r) and "+DEVTYPE:" in r and "ws2812" in r, repr(r))

    r = at.send("AT+HELP?")
    check("AT+HELP? lists the commands", ok_reply(r) and "+HELP:AT+DEV" in r, repr(r))

    print("Device configuration and control (through config_apply)")
    r = at.send(
        'AT+CFG=devices: [{id: at_relay, type: relay, config: {gpio: 5, active_level: 1}}]')
    check("AT+CFG=... applies a YAML document", ok_reply(r) and "+CFG:added,1" in r, repr(r))

    r = at.send("AT+DEV?")
    check("AT+DEV? lists the new device", ok_reply(r) and "at_relay" in r, repr(r))

    r = at.send('AT+DEV="at_relay",true')
    check("AT+DEV=\"id\",true actuates the device", ok_reply(r), repr(r))

    r = at.send('AT+DEV="at_relay"')
    check("AT+DEV=\"id\" reads it back as true",
          ok_reply(r) and '"state":true' in r.replace(" ", ""), repr(r))

    r = at.send('AT+DEV="at_relay",false')
    check("AT+DEV=\"id\",false releases it", ok_reply(r), repr(r))

    print("Argument handling")
    r = at.send('AT+DEV="nonexistent_device"')
    check("reading an unknown device fails cleanly", err_reply(r), repr(r))

    r = at.send('AT+DEV="at_relay",{not json')
    check("malformed JSON is rejected without a crash", err_reply(r), repr(r))

    print("Network / services")
    r = at.send("AT+CIFSR")
    check("AT+CIFSR reports IP and MAC",
          ok_reply(r) and "+CIFSR:STAIP" in r and "84:c7:bb" in r, repr(r))

    r = at.send("AT+SYSTIME?")
    check("AT+SYSTIME? reports the NTP time",
          ok_reply(r) and "+SYSTIME:2" in r, repr(r))

    r = at.send("AT+HOSTNAME?")
    check("AT+HOSTNAME? reports the mDNS name",
          ok_reply(r) and ".local" in r, repr(r))

    r = at.send("AT+CWMODE?")
    check("AT+CWMODE? reports the Wi-Fi mode", ok_reply(r) and "+CWMODE:" in r, repr(r))

    r = at.send("AT+MQTTCONN?")
    check("AT+MQTTCONN? reports the broker and connection state",
          ok_reply(r) and "+MQTTCONN:" in r, repr(r))

    r = at.send("AT+OTASTATUS?")
    check("AT+OTASTATUS? reports an idle OTA with the running version",
          ok_reply(r) and "+OTASTATUS:IDLE" in r, repr(r))

    print("Cleanup")
    r = at.send('AT+CFG=remove_devices: [at_relay]')
    check("AT+CFG removes the test device", ok_reply(r) and "+CFG:removed,1" in r, repr(r))

    at.close()

    failed = [l for l, ok in results if not ok]
    print()
    print(f"{len(results) - len(failed)}/{len(results)} checks passed")
    if failed:
        print("failed:", ", ".join(failed))
        return 1
    print("RESULT: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
