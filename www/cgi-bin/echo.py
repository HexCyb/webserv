#!/usr/bin/env python3
"""POST /cgi-bin/echo.py -> sends the request body back. The body arrives on stdin."""
import os
import sys

length = int(os.environ.get("CONTENT_LENGTH") or 0)
body = sys.stdin.buffer.read(length) if length > 0 else b""

sys.stdout.write("Content-Type: text/plain; charset=utf-8\r\n\r\n")
sys.stdout.write(f"method: {os.environ.get('REQUEST_METHOD')}\n")
sys.stdout.write(f"received: {len(body)} bytes\n")
sys.stdout.flush()
sys.stdout.buffer.write(body)
