#!/usr/bin/env python3
"""Send synthetic navigation messages to the Heltec TCP dev server.

Usage: python send_navigation.py 192.168.1.123 [--port 5050]
       python send_navigation.py 192.168.1.123 --once
This uses a TEST-ONLY ASCII format, not the production RideCast wire format.
"""
import argparse
import socket
import time

SAMPLES = [
    "PING\n",
    "NAV,RIGHT,250,35,1\n",
    "NAV,STRAIGHT,900,42,1\n",
    "NAV,LEFT,120,24,1\n",
    "NAV,ARRIVED,0,0,1\n",
]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("host", help="ESP32's DHCP IPv4 address")
    parser.add_argument("--port", type=int, default=5050)
    parser.add_argument("--once", action="store_true", help="One cycle and exit")
    args = parser.parse_args()

    with socket.create_connection((args.host, args.port), timeout=8) as sock:
        sock.settimeout(10)
        while True:
            for message in SAMPLES:
                print("TX", message.strip())
                sock.sendall(message.encode("ascii"))
                reply = bytearray()
                while not reply.endswith(b"\n"):
                    part = sock.recv(64)
                    if not part:
                        raise ConnectionError("ESP32 peer disconnected")
                    reply.extend(part)
                    if len(reply) > 128:
                        raise RuntimeError("Unexpected long response")
                print("RX", reply.decode("ascii", errors="replace").strip())
                time.sleep(2.0)
            if args.once:
                break


if __name__ == "__main__":
    main()
