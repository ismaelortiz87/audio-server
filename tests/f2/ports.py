#!/usr/bin/env python3
"""Print a free TCP+UDP port on 127.0.0.1, or exit non-zero.

The AOO server binds the same port for both TCP and UDP, so a port is only
usable if it can be bound in both protocols. Port 10999 (the server default
neighbour) was occupied during development, so nothing here is hardcoded.

The candidate port is released again before this script exits, which leaves a
small race; run.sh re-checks the chosen port with lsof immediately before
starting the server and retries if it lost the race.
"""

import socket
import sys


def candidate_ports(count=25):
    """Ask the kernel for ephemeral ports, then add a spread of fixed fallbacks."""
    seen = []
    for _ in range(count):
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        try:
            sock.bind(("127.0.0.1", 0))
            port = sock.getsockname()[1]
        finally:
            sock.close()
        if port not in seen:
            seen.append(port)
    # Fallbacks in case all ephemeral picks collide somewhere else.
    for port in range(21000, 21050):
        if port not in seen:
            seen.append(port)
    return seen


def port_free(port):
    """True if `port` can be bound for TCP *and* UDP on the loopback address."""
    for socktype in (socket.SOCK_STREAM, socket.SOCK_DGRAM):
        sock = socket.socket(socket.AF_INET, socktype)
        try:
            sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            sock.bind(("127.0.0.1", port))
        except OSError:
            return False
        finally:
            sock.close()
    return True


def main():
    for port in candidate_ports():
        if port_free(port):
            print(port)
            return 0
    print("no free TCP+UDP port found on 127.0.0.1", file=sys.stderr)
    return 1


if __name__ == "__main__":
    sys.exit(main())
