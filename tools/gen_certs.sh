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
#   tools/gen_certs.sh [CN] [DAYS]
#
# Defaults: CN=espx.local, DAYS=3650
#
# Notes:
#   * The bundled key is a DEVELOPMENT key. Replace it for production, or use a
#     proper PKI and provision per-device certificates.
#   * Browsers will warn about the self-signed certificate; the SAN list below
#     covers espx.local, the SoftAP gateway (192.168.4.1) and any hostname via
#     the DNS wildcard.
#
set -euo pipefail

CN="${1:-espx.local}"
DAYS="${2:-3650}"
DIR="$(cd "$(dirname "$0")/.." && pwd)/main/cert_manager/certs"

mkdir -p "$DIR"

echo "Generating certificate"
echo "  subject : CN=$CN"
echo "  validity: $DAYS days"
echo "  output  : $DIR/server.{crt,key}"

openssl req -x509 -newkey rsa:2048 -nodes \
    -keyout "$DIR/server.key" \
    -out    "$DIR/server.crt" \
    -days   "$DAYS" \
    -subj   "/CN=$CN/O=ESPX IoT/C=CN" \
    -addext "subjectAltName=DNS:$CN,DNS:espx,DNS:*,IP:192.168.4.1" \
    2>/dev/null

chmod 600 "$DIR/server.key"

echo
openssl x509 -in "$DIR/server.crt" -noout -subject -dates -ext subjectAltName | sed 's/^/  /'
echo
echo "Rebuild the firmware to embed the new certificate:  idf.py build"
