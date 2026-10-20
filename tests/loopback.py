# SPDX-License-Identifier: LGPL-3.0-or-later
# Copyright (C) 2026 Cavanshirpro
# Licensed under LGPL-3.0-or-later; see NOTICE, LICENSE and COPYING.
"""Dependency-free HTTP + RFC 6455 fixtures. Uses fake tokens and loopback only."""
import base64
import hashlib
import http.server
import json
import socket
import socketserver
import struct
import subprocess
import sys
import threading
import time
import zlib


def message(identifier="900", content="hello", author="100"):
    return {"id": identifier, "channel_id": "200", "author": {"id": author, "username": "cavan"}, "content": content}


class State:
    def __init__(self, mode):
        self.mode = mode
        self.connections = 0
        self.sends = 0
        self.heartbeats = 0
        self.errors = []
        self.operations = []


class Http(http.server.BaseHTTPRequestHandler):
    def log_message(self, *_):
        pass

    def do_GET(self):
        self.handle_api()

    do_POST = do_PATCH = do_PUT = do_DELETE = do_GET

    def handle_api(self):
        state = self.server.state
        try:
            assert self.headers.get("Authorization") == "offline-token", "Missing raw authorization"
            state.operations.append((self.command, self.path))
            length = int(self.headers.get("Content-Length", 0))
            body = json.loads(self.rfile.read(length)) if length else None
            status = 200
            if self.path == "/api/cancel":
                status, data = 429, {"retry_after": 30, "global": True}
            elif self.path == "/api/users/@me":
                data = {"id": "100", "username": "cavan"}
            elif self.path == "/api/users/@me/guilds?with_counts=true":
                data = [{"id": "400", "name": "fixture"}]
            elif self.path == "/api/guilds/400?with_counts=true":
                data = {"id": "400", "name": "fixture", "owner_id": "100", "approximate_member_count": 2}
            elif self.path == "/api/guilds/400/members/100":
                data = {"user": {"id": "100", "username": "cavan"}, "roles": []}
            elif self.path == "/api/users/100/profile?with_mutual_guilds=true&with_mutual_friends=false&with_mutual_friends_count=true&guild_id=400":
                data = {"user": {"id": "100", "username": "cavan"}, "user_profile": {"bio": "fixture"}}
            elif self.path == "/api/users/@me/relationships":
                data = [{"id": "100", "type": 5}]
            elif self.path == "/api/users/101":
                status, data = 403, {"code": 40001, "message": "Unauthorized"}
            elif self.path == "/api/users/101/profile?with_mutual_guilds=true&with_mutual_friends=false&with_mutual_friends_count=true":
                data = {"user": {"id": "101", "username": "peer"}, "user_profile": None}
            elif self.path == "/api/users/@me/relationships/100" and self.command == "PUT":
                assert body == {}, "Friend request must not silently block or change another relationship type"
                status, data = 204, None
            elif self.path == "/api/channels/200/messages" and self.command == "POST":
                state.sends += 1
                assert body["allowed_mentions"]["parse"] == []
                assert "embeds" not in body and "embed" not in body
                if state.sends == 1:
                    status, data = 429, {"retry_after": 0.18, "global": True}
                else:
                    data = message(content=body["content"])
                    if state.sends == 3:
                        assert body["message_reference"]["message_id"] == "900"
            elif self.path == "/api/channels/200/messages/900" and self.command == "PATCH":
                data = message(content=body["content"])
            elif self.path == "/api/channels/200/messages?limit=1":
                data = [message()]
            elif self.path.startswith("/api/channels/200/messages?limit=1&around="):
                identifier = self.path.rsplit("=", 1)[1]
                data = [] if identifier == "902" else [message()]
            elif self.path == "/api/channels/200" or self.path == "/api/users/@me/channels":
                if self.command == "POST":
                    assert body == {"recipients": ["100"]}, "Expected user-account DM payload"
                data = {"id": "200", "type": 1}
            elif self.command in ("DELETE", "PUT") or self.path.endswith("/typing"):
                status, data = 204, None
            else:
                raise AssertionError(f"Unexpected route {self.command} {self.path}")
            payload = json.dumps(data).encode() if status != 204 else b""
            self.send_response(status)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(payload)))
            self.end_headers()
            self.wfile.write(payload)
        except Exception as error:
            state.errors.append(str(error))
            self.send_error(500)


def exact(sock, length):
    output = b""
    while len(output) < length:
        data = sock.recv(length - len(output))
        if not data:
            raise EOFError()
        output += data
    return output


def receive_frame(sock):
    first, second = exact(sock, 2)
    length = second & 127
    if length == 126:
        length = struct.unpack("!H", exact(sock, 2))[0]
    elif length == 127:
        length = struct.unpack("!Q", exact(sock, 8))[0]
    assert length < 16 * 1024 * 1024
    mask = exact(sock, 4) if second & 128 else None
    data = exact(sock, length)
    if mask:
        data = bytes(value ^ mask[index % 4] for index, value in enumerate(data))
    return first & 15, data


def send_frame(sock, data, opcode=2, final=True):
    first = (128 if final else 0) | opcode
    length = len(data)
    header = bytes([first, length]) if length < 126 else bytes([first, 126]) + struct.pack("!H", length)
    sock.sendall(header + data)


class Gateway(socketserver.BaseRequestHandler):
    def handle(self):
        state = self.server.state
        state.connections += 1
        connection = state.connections
        sock = self.request
        sock.settimeout(4)
        try:
            headers = b""
            while b"\r\n\r\n" not in headers:
                headers += exact(sock, 1)
            key = next(line.split(b":", 1)[1].strip() for line in headers.split(b"\r\n") if line.lower().startswith(b"sec-websocket-key:"))
            accept = base64.b64encode(hashlib.sha1(key + b"258EAFA5-E914-47DA-95CA-C5AB0DC85B11").digest())
            sock.sendall(b"HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: " + accept + b"\r\n\r\n")
            compressor = zlib.compressobj()
            seq = 0

            def packet(op, data, name=None, fragmented=False):
                nonlocal seq
                value = {"op": op, "d": data}
                if name:
                    seq += 1
                    value.update(t=name, s=seq)
                payload = compressor.compress(json.dumps(value).encode()) + compressor.flush(zlib.Z_SYNC_FLUSH)
                if fragmented:
                    # Divide both a WebSocket message and a zlib flush across messages.
                    cut = max(1, len(payload) // 2)
                    send_frame(sock, payload[:cut], final=False)
                    send_frame(sock, b"fixture", opcode=9)
                    send_frame(sock, payload[cut:-2], opcode=0)
                    send_frame(sock, payload[-2:])
                else:
                    send_frame(sock, payload)

            if state.mode != "no_hello":
                packet(10, {"heartbeat_interval": 40}, fragmented=True)
            identified = False
            start = None
            triggered = False
            while True:
                opcode, data = receive_frame(sock)
                if opcode == 8:
                    return
                if opcode in (9, 10):
                    continue
                value = json.loads(data)
                if value["op"] == 1:
                    state.heartbeats += 1
                    if state.mode != "missing_ack":
                        packet(11, None)
                elif value["op"] in (2, 6):
                    assert value["d"]["token"] == "offline-token"
                    identified, start = True, time.monotonic()
                    if state.mode == "fatal":
                        send_frame(sock, struct.pack("!H", 4004), opcode=8)
                        return
                    if state.mode == "no_ready":
                        continue
                    if connection == 2 and state.mode == "basic":
                        assert value["op"] == 6 and value["d"]["session_id"] == "session"
                        packet(0, {}, "RESUMED")
                    else:
                        assert value["op"] == 2, "Invalid session must identify"
                        assert value["d"]["presence"]["status"] == "unknown", "Must not force the account online"
                        packet(0, {"user": {"id": "100", "username": "cavan"}, "guilds": [], "session_id": "session", "resume_gateway_url": self.server.url}, "READY", fragmented=True)
                    if state.mode == "basic" and connection == 1:
                        packet(0, {"id": "100", "global_name": "updated"}, "USER_UPDATE")
                        packet(0, message("300", '!echo "two words" three'), "MESSAGE_CREATE")
                        packet(0, message("301", '!echo "two words" three', "999"), "MESSAGE_CREATE")
                        packet(0, {"id": "300", "channel_id": "200", "content": "changed", "edited_timestamp": None}, "MESSAGE_UPDATE")
                        packet(0, {"ids": ["300", "301"], "channel_id": "200"}, "MESSAGE_DELETE_BULK")
                    if state.mode == "overflow":
                        for _ in range(30):
                            packet(0, {}, "UNKNOWN")
                if identified and not triggered and time.monotonic() - start > 0.30:
                    triggered = True
                    if state.mode == "basic" and connection == 1:
                        assert state.heartbeats >= 3, "REST blocked heartbeats"
                        packet(7, None)
                        return
                    if state.mode == "invalid" and connection == 1:
                        packet(9, False)
                        return
                    if state.mode in ("basic", "invalid", "rerun"):
                        packet(0, {}, "FINISH")
        except (EOFError, ConnectionError):
            pass
        except Exception as error:
            state.errors.append(f"Gateway: {error!r}")


class TcpServer(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True


def main():
    for mode in ("basic", "invalid", "fatal", "missing_ack", "callback", "overflow", "cancel", "rerun", "no_ready", "no_hello"):
        state = State(mode)
        with http.server.ThreadingHTTPServer(("127.0.0.1", 0), Http) as api_server, TcpServer(("127.0.0.1", 0), Gateway) as gateway:
            api_server.state = gateway.state = state
            gateway.url = f"ws://127.0.0.1:{gateway.server_address[1]}/"
            threading.Thread(target=api_server.serve_forever, daemon=True).start()
            threading.Thread(target=gateway.serve_forever, daemon=True).start()
            result = subprocess.run([sys.argv[1], f"http://127.0.0.1:{api_server.server_address[1]}/api", gateway.url, mode], capture_output=True, text=True, timeout=12)
            api_server.shutdown()
            gateway.shutdown()
            if result.returncode or state.errors:
                raise RuntimeError(f"{mode}: {result.stdout} {result.stderr} {state.errors}")
            if mode == "basic":
                assert state.sends == 3 and state.connections == 2
                assert ("PUT", "/api/channels/200/messages/900/reactions/a%3Ab/@me") in state.operations
            print(result.stdout.strip())


if __name__ == "__main__":
    main()
