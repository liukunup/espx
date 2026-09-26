#!/usr/bin/env bash
#
# Regenerate the ESPX HTTPS server certificate.
#
# The certificate and key live in the project as plain PEM files so they can be
# reviewed, replaced and version-controlled independently of the C code. They
# are compiled into the firmware at build time (see main/CMakeLists.txt,
# EMBED_FILES).
#
# Usage:
#   tools/gen_certs.sh [CN] [DAYS] [KEYTYPE]
#
# Defaults: CN=espx.local, DAYS=3650, KEYTYPE=ecdsa
#
# KEYTYPE:
#   ecdsa  (default) ECDSA P-256. An ECDSA server signature costs a few
#          milliseconds instead of the hundreds an RSA-2048 private-key
#          operation takes on this class of CPU. With RSA the TLS handshake
#          measured ~1.5 s on an ESP32-S3; with ECDSA it drops to a fraction of
#          that, which is the difference between a snappy and a sluggish UI.
#          Requires a client from the last decade (all current browsers, curl,
#          python ssl).
#   rsa    RSA-2048, for clients that cannot do ECDSA.
#
# Notes:
#   * The bundled key is a DEVELOPMENT key. Replace it for production, or use a
#     proper PKI and provision per-device certificates.
#   * Browsers will warn about the self-signed certificate; the SAN list below
#     covers <CN>, any *.local name (so espx-<mac>.local matches), and the SoftAP
#     gateway. Note the wildcard must have something to match: "DNS:*" is a
#     malformed SAN that some clients reject with a fatal alert.
#
set -euo pipefail

CN="${1:-espx.local}"
DAYS="${2:-3650}"
KEYTYPE="${3:-ecdsa}"
DIR="$(cd "$(dirname "$0")/.." && pwd)/main/cert_manager/certs"

mkdir -p "$DIR"

# Note: openssl's -newkey does NOT accept "ec:prime256v1" (it reads the part
# after the colon as a parameter FILE). The curve must be given through
# -pkeyopt, which is only valid for the "ec" key type.
case "$KEYTYPE" in
    ecdsa) NEWKEY_ARGS=(-newkey ec -pkeyopt ec_paramgen_curve:prime256v1) ;;
    rsa)   NEWKEY_ARGS=(-newkey rsa:2048) ;;
    *)     echo "error: KEYTYPE must be 'ecdsa' or 'rsa'" >&2; exit 1 ;;
esac

echo "Generating certificate"
echo "  subject : CN=$CN"
echo "  key     : $KEYTYPE"
echo "  validity: $DAYS days"
echo "  output  : $DIR/server.{crt,key}"

# Generate to a temporary pair first: a failure must not leave the previous
# certificate in place looking as if it had been regenerated.
TMPDIR_CERT="$(mktemp -d)"
trap 'rm -rf "$TMPDIR_CERT"' EXIT

if ! openssl req -x509 "${NEWKEY_ARGS[@]}" -nodes \
        -keyout "$TMPDIR_CERT/server.key" \
        -out    "$TMPDIR_CERT/server.crt" \
        -days   "$DAYS" \
        -subj   "/CN=$CN/O=ESPX IoT/C=CN" \
        -addext "subjectAltName=DNS:$CN,DNS:*.local,DNS:espx,IP:192.168.4.1"; then
    echo "error: openssl failed; the existing certificate was left untouched" >&2
    exit 1
fi

install -m 600 "$TMPDIR_CERT/server.key" "$DIR/server.key"
install -m 644 "$TMPDIR_CERT/server.crt" "$DIR/server.crt"

echo
openssl x509 -in "$DIR/server.crt" -noout -subject -dates \
    -ext subjectAltName 2>/dev/null | sed 's/^/  /'
openssl x509 -in "$DIR/server.crt" -noout -text 2>/dev/null \
    | grep -iE "public key algorithm|NIST CURVE" | sed 's/^ */  /'
echo
echo "Rebuild the firmware to embed the new certificate:  idf.py build"
