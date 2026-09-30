#!/usr/bin/env python3
"""Gzip compress index.html to index.html.gz"""

import gzip
import os
import sys

script_dir = os.path.dirname(os.path.abspath(__file__))
project_root = os.path.dirname(script_dir)
html_file = os.path.join(project_root, "main", "web_server", "web_files", "index.html")
gz_file = os.path.join(project_root, "main", "web_server", "web_files", "index.html.gz")

if os.path.exists(html_file):
    with open(html_file, "rb") as f:
        content = f.read()
    with gzip.open(gz_file, "wb", compresslevel=9) as f:
        f.write(content)
    ratio = os.path.getsize(gz_file) * 100 // len(content)
    print(f"[ESPX] Gzip: {os.path.getsize(gz_file)}/{len(content)} bytes ({ratio}%)")
else:
    print(f"[ESPX] Warning: {html_file} not found", file=sys.stderr)
