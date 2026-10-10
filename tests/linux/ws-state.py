#!/usr/bin/env python3
"""Prints the agent's full `state` message (docs/control-api.md 3.3) as one JSON line.

Minimal stdlib WebSocket client (no deps) for tests/linux/deb-systemd.sh:
connects to ws://127.0.0.1:<port>/api/v1/ws, reads frames until `t == "state"`.
Usage: ws-state.py [port]   (default 7071). Exit 1 if no state arrives in 5 s.
"""
import base64, json, os, socket, struct, sys

port = int(sys.argv[1]) if len(sys.argv) > 1 else 7071
s = socket.create_connection(("127.0.0.1", port), timeout=5)
key = base64.b64encode(os.urandom(16)).decode()
s.sendall(("GET /api/v1/ws HTTP/1.1\r\nHost: localhost\r\nUpgrade: websocket\r\n"
           "Connection: Upgrade\r\nSec-WebSocket-Key: %s\r\nSec-WebSocket-Version: 13\r\n\r\n" % key).encode())
buf = b""
while b"\r\n\r\n" not in buf:
    d = s.recv(4096)
    if not d:
        sys.exit(1)
    buf += d
head, buf = buf.split(b"\r\n\r\n", 1)
if b" 101 " not in head.split(b"\r\n")[0]:
    sys.exit(1)


def need(n):
    global buf
    while len(buf) < n:
        d = s.recv(65536)
        if not d:
            raise EOFError
        buf += d
    out, buf = buf[:n], buf[n:]
    return out


try:
    msg = b""
    while True:
        b0, b1 = need(2)
        n = b1 & 0x7F
        if n == 126:
            n = struct.unpack(">H", need(2))[0]
        elif n == 127:
            n = struct.unpack(">Q", need(8))[0]
        payload = need(n)            # server frames are unmasked
        op = b0 & 0x0F
        if op == 8:
            sys.exit(1)
        if op in (1, 0):             # text / continuation
            msg += payload
            if b0 & 0x80:
                obj = json.loads(msg.decode())
                msg = b""
                if obj.get("t") == "state":
                    print(json.dumps(obj.get("state", obj)))
                    sys.exit(0)
except (EOFError, socket.timeout):
    sys.exit(1)
