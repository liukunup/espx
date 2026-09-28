#!/usr/bin/env python3
"""
Generate ESPX device certificate with proper subject from device ID.

Usage:
    python tools/gen_cert.py [--id <device-id>]

If --id is not provided, reads MAC address from device via serial
or generates from local network interfaces.

Examples:
    python tools/gen_cert.py
    python tools/gen_cert.py --id espx-84c7bb772e74
    python tools/gen_cert.py --mac 84:c7:bb:77:2e:74
"""

import argparse
import os
import subprocess
import sys
import tempfile
import shutil

CERT_DIR = "main/cert_manager/certs"
OPENSSL_CNF = """
[req]
default_bits        = 256
default_md          = sha256
distinguished_name  = req_distinguished_name
x509_extensions     = v3_ca
prompt              = no

[req_distinguished_name]
CN = ESPX

[v3_ca]
subjectAltName = @alt_names

[alt_names]
DNS.1 = {cn}
DNS.2 = espx.local
DNS.3 = *.local
DNS.4 = espx
IP.1 = 192.168.4.1
"""


def get_mac_addresses():
    """Get MAC addresses from local network interfaces."""
    macs = []
    for iface in ['en0', 'en1', 'eth0', 'wlan0', 'wlp2s0']:
        try:
            if sys.platform == 'darwin':
                result = subprocess.run(
                    [' networksetup', '-getmacaddress', iface],
                    capture_output=True, text=True
                )
            else:
                result = subprocess.run(
                    ['cat', f'/sys/class/net/{iface}/address'],
                    capture_output=True, text=True
                )
            if result.returncode == 0:
                mac = result.stdout.strip()
                if mac and ':' in mac:
                    macs.append(mac.lower().replace(':', ''))
        except:
            pass
    return macs


def get_device_id_from_serial():
    """Read device ID from serial connection.

    Not supported: the firmware no longer exposes a serial AT command
    interface, so there is no AT+ID? query to issue. Device IDs are
    derived from the MAC (see mac_to_device_id) or supplied explicitly.
    """
    return None


def mac_to_device_id(mac: str) -> str:
    """Convert MAC address to device ID."""
    # Remove separators
    mac_clean = mac.replace(':', '').replace('-', '').lower()
    # Take first 12 chars (6 bytes MAC)
    mac_hex = mac_clean[:12]
    return f"espx-{mac_hex}"


def generate_openssl_config(cn: str) -> str:
    """Generate OpenSSL config file content."""
    return OPENSSL_CNF.format(cn=cn)


def generate_cert(device_id: str, days: int = 3650, keytype: str = 'ecdsa'):
    """Generate certificate for the device."""
    cn = f"{device_id}.local"
    
    print(f"Generating certificate for {device_id}")
    print(f"  CN         : {cn}")
    print(f"  key type   : {keytype}")
    print(f"  validity   : {days} days")
    print(f"  output dir : {CERT_DIR}")
    
    # Create output directory
    os.makedirs(CERT_DIR, exist_ok=True)
    
    # Create temp directory
    tmpdir = tempfile.mkdtemp()
    
    try:
        # Write OpenSSL config
        config_file = os.path.join(tmpdir, 'openssl.cnf')
        with open(config_file, 'w') as f:
            f.write(generate_openssl_config(cn))
        
        # Generate key and certificate
        key_file = os.path.join(tmpdir, 'server.key')
        cert_file = os.path.join(tmpdir, 'server.crt')
        
        if keytype == 'ecdsa':
            # ECDSA P-256 key
            subprocess.run([
                'openssl', 'req', '-x509', '-newkey', 'ec',
                '-pkeyopt', 'ec_paramgen_curve:prime256v1',
                '-nodes',
                '-keyout', key_file,
                '-out', cert_file,
                '-days', str(days),
                '-subj', f'/CN={cn}/O=ESPX/C=CN',
                '-addext', f'subjectAltName=DNS:{cn},DNS:*.local,DNS:espx,IP:192.168.4.1',
                '-config', config_file,
            ], check=True)
        else:
            # RSA 2048 key
            subprocess.run([
                'openssl', 'req', '-x509', '-newkey', 'rsa:2048',
                '-nodes',
                '-keyout', key_file,
                '-out', cert_file,
                '-days', str(days),
                '-subj', f'/CN={cn}/O=ESPX IoT/C=CN',
                '-addext', f'subjectAltName=DNS:{cn},DNS:*.local,DNS:espx,IP:192.168.4.1',
                '-config', config_file,
            ], check=True)
        
        # Copy to output directory
        shutil.copy2(key_file, os.path.join(CERT_DIR, 'server.key'))
        shutil.copy2(cert_file, os.path.join(CERT_DIR, 'server.crt'))
        
        # Set permissions
        os.chmod(os.path.join(CERT_DIR, 'server.key'), 0o600)
        
        # Display certificate info
        print("\nCertificate generated successfully!")
        print("\nCertificate info:")
        result = subprocess.run(
            ['openssl', 'x509', '-in', cert_file, '-noout', '-subject', '-dates'],
            capture_output=True, text=True
        )
        for line in result.stdout.strip().split('\n'):
            print(f"  {line}")
        
        print(f"\nRebuild the firmware: idf.py build")
        
    finally:
        shutil.rmtree(tmpdir)


def main():
    parser = argparse.ArgumentParser(description='Generate ESPX device certificate')
    parser.add_argument('--id', help='Device ID (e.g., espx-84c7bb772e74)')
    parser.add_argument('--mac', help='MAC address (e.g., 84:c7:bb:77:2e:74)')
    parser.add_argument('--days', type=int, default=3650, help='Validity in days (default: 3650)')
    parser.add_argument('--keytype', choices=['ecdsa', 'rsa'], default='ecdsa',
                        help='Key type (default: ecdsa)')
    
    args = parser.parse_args()
    
    device_id = args.id
    
    if not device_id and args.mac:
        device_id = mac_to_device_id(args.mac)
    elif not device_id:
        # Try to get from serial
        device_id = get_device_id_from_serial()
        
        if not device_id:
            # Use first available MAC
            macs = get_mac_addresses()
            if macs:
                device_id = mac_to_device_id(macs[0])
                print(f"No device ID specified, using local MAC: {device_id}")
            else:
                # Default for development
                device_id = "espx-84c7bb772e74"
                print(f"No MAC found, using default: {device_id}")
    
    generate_cert(device_id, args.days, args.keytype)


if __name__ == '__main__':
    main()
