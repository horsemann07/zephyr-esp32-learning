"""Minimal RideCast-08 Wi-Fi client (dev LAN only, NOT TLS).
Usage: python wifi_app_test.py <ESP32-IP>
Default stores the raw Wi-Fi demo token next to this script (excluded in Git).
"""
import argparse
from pathlib import Path
import socket

TOKEN_FILE = Path(__file__).resolve().parent / "ridecast_wifi_token.txt"


def readline(sock):
    data = bytearray()
    while len(data) < 128:
        ch = sock.recv(1)
        if not ch:
            raise ConnectionError("Peer disconnected")
        if ch == b"\n":
            return data.decode("ascii").strip()
        data.extend(ch)
    raise ValueError("Response too long")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("ip", help="ESP32 IPv4 address on your trusted LAN")
    parser.add_argument("--port", type=int, default=5050)
    args = parser.parse_args()
    with socket.create_connection((args.ip, args.port), timeout=12) as sock:
        sock.settimeout(12)
        prompt = readline(sock)
        print("ESP32:", prompt)
        if prompt == "ERR BUSY":
            raise SystemExit("Another authorized client or pending pairing is active")
        if prompt == "PIN_REQUIRED":
            code = input("Enter the 6-digit PIN shown on OLED: ").strip()
            if len(code) != 6 or not code.isascii() or not code.isdigit():
                raise ValueError("PIN must be exactly six ASCII digits")
            sock.sendall(("AUTH " + code + "\n").encode("ascii"))
            result = readline(sock)
            if not result.startswith("OK TOKEN "):
                raise SystemExit("Enrollment rejected: " + result)
            token = result[len("OK TOKEN "):]
            if len(token) != 32:
                raise ValueError("Bad token response")
            TOKEN_FILE.write_text(token, encoding="ascii")
            print("Registration saved. Token at:", TOKEN_FILE)
        elif prompt == "TOKEN_REQUIRED":
            if not TOKEN_FILE.exists():
                raise SystemExit("No token file. Clear the ESP32 registration or reuse original client token.")
            token = TOKEN_FILE.read_text(encoding="ascii").strip()
            sock.sendall(("TOKEN " + token + "\n").encode("ascii"))
            result = readline(sock)
            if result != "OK CONNECTED":
                raise SystemExit("Authentication failed: " + result)
            print("Returning Wi-Fi client authenticated")
        else:
            raise RuntimeError("Unexpected response: " + prompt)
        sock.sendall(b"PING\n")
        print("PING reply:", readline(sock))
        input("Authenticated session active. Press ENTER to disconnect ...")


if __name__ == "__main__":
    main()
