"""Listen for the optional RideCast-08 UDP discovery beacon on port 39801."""
import socket

with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
    sock.bind(("", 39801))
    sock.settimeout(90)
    print("Listening for RideCast-08 UDP beacons... (allow Windows firewall access)")
    try:
        while True:
            payload, (ip, port) = sock.recvfrom(256)
            if payload.startswith(b"RIDECAST08,"):
                print("Device IP:", ip, "Service:", payload.decode("ascii"))
    except socket.timeout:
        print("No discovery beacon. Check Wi-Fi / AP isolation or use router to find IP.")
