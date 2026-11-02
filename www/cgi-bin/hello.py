#!/usr/bin/env python3
"""GET /cgi-bin/hello.py?name=42 -> a page built from the query string and the CGI environment."""
import html
import os
from urllib.parse import parse_qs

name = parse_qs(os.environ.get("QUERY_STRING", "")).get("name", ["world"])[0]

print("Content-Type: text/html; charset=utf-8")
print()
print("<!DOCTYPE html><html><body style='font-family:monospace;background:#0d1117;color:#c9d1d9;padding:40px'>")
print(f"<h1>Hello, {html.escape(name)}!</h1>")
print("<table>")
for key in ("REQUEST_METHOD", "QUERY_STRING", "SCRIPT_NAME", "PATH_INFO", "SERVER_PORT", "REMOTE_ADDR"):
    print(f"<tr><td>{key}</td><td>{html.escape(os.environ.get(key, ''))}</td></tr>")
print("</table></body></html>")
