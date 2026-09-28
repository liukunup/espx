#!/usr/bin/env python3
"""Sample /api/system/info and print internal-RAM / PSRAM / stack headroom.

Used to compare a refactor against a baseline. Usage:
    tools/sysinfo_baseline.py --host 192.168.1.50 --samples 10
"""

import argparse
import json
import ssl
import sys
import time
import urllib.request


def fetch(host):
    ctx = ssl._create_unverified_context()  # self-signed dev certificate
    with urllib.request.urlopen(f"https://{host}/api/system/info",
                                context=ctx, timeout=5) as r:
        return json.load(r)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", required=True)
    ap.add_argument("--samples", type=int, default=5)
    ap.add_argument("--json-out")
    args = ap.parse_args()

    rows = []
    for i in range(args.samples):
        try:
            rows.append(fetch(args.host))
        except Exception as exc:                      # noqa: BLE001
            sys.exit(f"sample {i} failed: {exc}")
        time.sleep(1)

    first = rows[0]
    ram = first.get("ram", {})
    print(f"iram_free  {ram.get('iram_free')}")
    print(f"iram_min   {ram.get('iram_min')}   (lowest seen - leak indicator)")
    print(f"iram_total {ram.get('iram_total')}")
    print(f"psram_free {ram.get('psram_free')}")
    print(f"psram_total {ram.get('psram_total')}")
    print("\ntasks (stack_free_bytes):")
    for t in sorted(first.get("tasks", []),
                    key=lambda x: x.get("stack_free_bytes", 0)):
        print(f"  {t.get('name'):<16} {t.get('stack_free_bytes')}")

    if args.json_out:
        with open(args.json_out, "w") as fh:
            json.dump(rows, fh, indent=2)
        print(f"\nwrote {args.json_out}")


if __name__ == "__main__":
    main()
