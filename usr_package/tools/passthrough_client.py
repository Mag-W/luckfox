#!/usr/bin/env python3
import argparse
import socket
import struct
from itertools import count

CMD_ECHO = 1
CMD_GET_STATUS = 2

_req_id_gen = count(1)


def readn(sock: socket.socket, n: int) -> bytes:
    data = b""
    while len(data) < n:
        chunk = sock.recv(n - len(data))
        if not chunk:
            raise ConnectionError("socket closed by peer")
        data += chunk
    return data


def build_request(cmd: int, payload: bytes):
    req_id = next(_req_id_gen) & 0xFFFFFFFF
    body = struct.pack("!IHH", req_id, cmd, len(payload)) + payload
    frame = struct.pack("!I", len(body)) + body
    return req_id, frame


def send_request(host: str, port: int, cmd: int, payload: bytes, timeout: float = 5.0):
    req_id, frame = build_request(cmd, payload)

    with socket.create_connection((host, port), timeout=timeout) as sock:
        sock.settimeout(timeout)
        sock.sendall(frame)

        frame_len = struct.unpack("!I", readn(sock, 4))[0]
        if frame_len < 10:
            raise RuntimeError(f"bad response frame_len={frame_len}")

        body = readn(sock, frame_len)
        rsp_req_id, rsp_cmd, code, payload_len = struct.unpack("!IHHH", body[:10])
        rsp_payload = body[10:]

        if len(rsp_payload) != payload_len:
            raise RuntimeError(
                f"payload_len mismatch: header={payload_len}, actual={len(rsp_payload)}"
            )
        if rsp_req_id != req_id:
            raise RuntimeError(f"request_id mismatch: {rsp_req_id} != {req_id}")

        return {
            "request_id": rsp_req_id,
            "cmd": rsp_cmd,
            "code": code,
            "payload": rsp_payload,
        }


def main():
    parser = argparse.ArgumentParser(description="Passthrough test client")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=19000)
    parser.add_argument("--cmd", choices=["echo", "status"], default="echo")
    parser.add_argument("--text", default="hello luckfox")
    args = parser.parse_args()

    if args.cmd == "echo":
        result = send_request(args.host, args.port, CMD_ECHO, args.text.encode("utf-8"))
    else:
        result = send_request(args.host, args.port, CMD_GET_STATUS, b"")

    print("response:")
    print(f"  request_id = {result['request_id']}")
    print(f"  cmd        = {result['cmd']}")
    print(f"  code       = {result['code']}")
    try:
        print(f"  payload    = {result['payload'].decode('utf-8')}")
    except UnicodeDecodeError:
        print(f"  payload(hex)= {result['payload'].hex()}")


if __name__ == "__main__":
    main()
