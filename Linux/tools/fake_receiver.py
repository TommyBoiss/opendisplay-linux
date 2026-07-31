#!/usr/bin/env python3
"""Minimal OpenDisplay receiver for host-side transport smoke tests."""

import argparse
import json
import socket
import struct
import time


def send_message(connection: socket.socket, message: dict[str, object]) -> None:
    payload = json.dumps(message, separators=(",", ":")).encode()
    connection.sendall(struct.pack(">I", len(payload)) + payload)


def receive_exact(connection: socket.socket, size: int) -> bytes:
    result = bytearray()
    while len(result) < size:
        chunk = connection.recv(size - len(result))
        if not chunk:
            raise EOFError
        result.extend(chunk)
    return bytes(result)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=9000)
    parser.add_argument("--width", type=int, default=1280)
    parser.add_argument("--height", type=int, default=800)
    args = parser.parse_args()
    with socket.socket() as listener:
        listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        listener.bind(("0.0.0.0", args.port))
        listener.listen(1)
        print(f"fake receiver listening on :{args.port}", flush=True)
        connection, address = listener.accept()
        with connection:
            print(f"connected from {address}", flush=True)
            send_message(connection, {
                "type": "hello", "pixelsWide": args.width,
                "pixelsHigh": args.height, "scale": 2,
                "device": "Fake iPad", "id": "linux-smoke", "pv": 2,
            })
            frames = 0
            last_report = time.monotonic()
            while True:
                try:
                    size = struct.unpack(">I", receive_exact(connection, 4))[0]
                    payload = receive_exact(connection, size)
                except EOFError:
                    break
                if payload.startswith(b"{") and b"\x00\x00\x00\x01" not in payload:
                    print(payload.decode(errors="replace"), flush=True)
                    continue
                if b"\x00\x00\x00\x01" in payload:
                    frames += 1
                if time.monotonic() - last_report >= 1:
                    print(f"video frames: {frames}", flush=True)
                    last_report = time.monotonic()


if __name__ == "__main__":
    main()
