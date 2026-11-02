#!/usr/bin/env python3
"""Load test: many clients at the same time, every answer must be 200 OK.

usage: python3 tests/stress.py [clients] [requests-per-client]   (webserv must be running)
"""
import http.client
import sys
import threading
import time

HOST, PORT = "127.0.0.1", 8080
CLIENTS = int(sys.argv[1]) if len(sys.argv) > 1 else 100
REQUESTS = int(sys.argv[2]) if len(sys.argv) > 2 else 50

ok = 0
failed = 0
lock = threading.Lock()


def client():
    global ok, failed
    good = 0
    try:
        conn = http.client.HTTPConnection(HOST, PORT, timeout=10)
        for _ in range(REQUESTS):
            conn.request("GET", "/")
            answer = conn.getresponse()
            answer.read()
            if answer.status == 200:
                good += 1
        conn.close()
    except Exception:
        pass
    with lock:
        ok += good
        failed += REQUESTS - good


start = time.time()
threads = [threading.Thread(target=client) for _ in range(CLIENTS)]
for t in threads:
    t.start()
for t in threads:
    t.join()
total = CLIENTS * REQUESTS
print(f"{ok}/{total} OK, {failed} failed, {total / (time.time() - start):.0f} requests/s")
sys.exit(0 if failed == 0 else 1)
