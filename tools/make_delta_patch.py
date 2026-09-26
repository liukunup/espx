#!/usr/bin/env python3
#
# ESPX delta OTA patch generator.
#
# Produces a patch file in the format expected by main/ota_service:
#
#   offset  0 ..  3 : magic 0xfccdde10  (little endian)
#   offset  4 .. 35 : SHA-256 of the BASE firmware image (32 bytes)
#   offset 36 .. 63 : reserved (28 bytes, zero)
#   offset 64 ..    : detools patch (heatshrink compressed)
#
# The digest is the app image's own SHA-256 (esptool "Validation hash"),
# which is exactly what esp_partition_get_sha256() returns for an app
# partition on the device. The device rejects the patch if it does not match
# the firmware it is currently running.
#
# Usage:
#   tools/make_delta_patch.py --base build/espx-v1.0.0.bin \
#                             --new  build/espx-v1.0.1.bin \
#                             --out  dist/espx-v1.0.0_to_v1.0.1.patch \
#                             [--chip esp32s3]
#
# Requires: detools >= 0.49.0, esptool (both present in the ESP-IDF env)

import argparse
import hashlib
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

MAGIC = 0xFCCDDE10
MAGIC_SIZE = 4
DIGEST_SIZE = 32
HEADER_SIZE = 64
RESERVED_SIZE = HEADER_SIZE - MAGIC_SIZE - DIGEST_SIZE

try:
    import detools
except ImportError:
    sys.exit("error: 'detools' not found. Install with: pip install 'detools>=0.49.0'")


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 16), b""):
            h.update(chunk)
    return h.hexdigest()


def image_validation_hash(binary, chip):
    """Return the app image SHA-256 (esptool 'Validation hash')."""
    # Preferred: read the digest appended to the image (last 32 bytes are the
    # hash only when the image was built with hash_appended, which is the
    # ESP-IDF default for app images).
    try:
        out = subprocess.run(
            [sys.executable, "-m", "esptool", "--chip", chip, "image_info", str(binary)],
            capture_output=True, text=True, check=False,
        ).stdout
    except Exception as exc:  # pragma: no cover
        sys.exit(f"error: failed to run esptool: {exc}")

    m = re.search(r"Validation hash:\s*([0-9a-fA-F]{64})\s*\(valid\)", out)
    if not m:
        sys.exit(
            "error: could not determine the image validation hash.\n"
            "       The base binary must be a complete ESP-IDF app image.\n"
            f"       esptool said:\n{out}"
        )
    return m.group(1).lower()


def build_patch(base: Path, new: Path, out: Path, chip: str) -> None:
    digest_hex = image_validation_hash(base, chip)
    digest = bytes.fromhex(digest_hex)

    tmp = Path(tempfile.mkdtemp(prefix="espx-patch-"))
    try:
        raw_patch = tmp / "patch.bin"
        with open(base, "rb") as b, open(new, "rb") as n, open(raw_patch, "wb") as p:
            detools.create_patch(b, n, p, compression="heatshrink")

        out.parent.mkdir(parents=True, exist_ok=True)
        with open(out, "wb") as f:
            f.write(MAGIC.to_bytes(MAGIC_SIZE, "little"))
            f.write(digest)
            f.write(bytes(RESERVED_SIZE))
            f.write(raw_patch.read_bytes())

        print(f"base digest : {digest_hex[:32]}…")
        print(f"base        : {base} ({base.stat().st_size} bytes)")
        print(f"new         : {new} ({new.stat().st_size} bytes)")
        print(f"patch       : {out} ({out.stat().st_size} bytes, "
              f"{100 * (1 - out.stat().st_size / base.stat().st_size):.1f}% smaller)")

        verify(base, out, new, tmp)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def verify(base: Path, patch: Path, new: Path, tmp: Path) -> None:
    with open(patch, "rb") as f:
        f.seek(HEADER_SIZE)
        payload = f.read()

    payload_file = tmp / "payload.bin"
    payload_file.write_bytes(payload)

    reconstructed = tmp / "reconstructed.bin"
    detools.apply_patch_filenames(str(base), str(payload_file), str(reconstructed))

    if sha256_file(reconstructed) == sha256_file(new):
        print("verify      : OK (patch reconstructs the new image exactly)")
    else:
        sys.exit("error: verification failed — reconstructed image differs from the new image")


def main():
    ap = argparse.ArgumentParser(description="Generate an ESPX delta OTA patch")
    ap.add_argument("--base", required=True, type=Path, help="firmware currently on the device")
    ap.add_argument("--new", required=True, type=Path, help="firmware to update to")
    ap.add_argument("--out", required=True, type=Path, help="output .patch path")
    ap.add_argument("--chip", default="esp32s3", help="target chip (default: esp32s3)")
    args = ap.parse_args()

    for f in (args.base, args.new):
        if not f.is_file():
            sys.exit(f"error: file not found: {f}")

    build_patch(args.base, args.new, args.out, args.chip)

    print()
    print("Serve and apply:")
    print(f"  python3 -m http.server 8000 --directory {args.out.parent}")
    print(f"  curl -k -X POST https://<device-ip>/api/ota/start \\")
    print(f"       -H 'Content-Type: application/json' \\")
    print(f"       -d '{{\"url\":\"http://<your-ip>:8000/{args.out.name}\"}}'")


if __name__ == "__main__":
    main()
