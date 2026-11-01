# SPDX-License-Identifier: LGPL-3.0-or-later
# Copyright (C) 2026 Cavanshirpro
# Licensed under LGPL-3.0-or-later; see NOTICE, LICENSE and COPYING.
"""Real libcurl session reuse, method isolation and credential boundaries on localhost."""
import http.server
import json
import subprocess
import sys
import threading


class Http(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, *_):
        pass

    def handle_api(self):
        server = self.server
        try:
            length = int(self.headers.get("Content-Length", 0))
            body = self.rfile.read(length)
            cookie = self.headers.get("Cookie", "")
            auth = self.headers.get("Authorization", "")
            assert self.headers.get("User-Agent", "").startswith("discord.cpp-self/")
            if self.path == "/api/session/start":
                assert self.command == "POST" and json.loads(body) == {"fixture": True}
                assert auth == "fixture-account-a" and not cookie
            elif self.path == "/api/session/continue":
                assert self.command == "GET" and not body
                assert auth == "fixture-account-a" and "fixture_session=account-a" in cookie
                assert "out_of_scope" not in cookie
                assert server.connections[0] == self.client_address, "HTTP connection was not reused"
            elif self.path == "/api/session/new-account":
                assert auth == "fixture-account-b" and not cookie, "Cookies crossed credentials"
                assert server.connections[0] != self.client_address, "Old credential transport survived"
            elif self.path == "/api/session/independent":
                assert auth == "fixture-account-a" and not cookie, "Cookies crossed Client instances"
            else:
                raise AssertionError("Unexpected endpoint")
            server.connections.append(self.client_address)
            self.send_response(200)
            if self.path == "/api/session/start":
                self.send_header("Set-Cookie", "fixture_session=account-a; Path=/api/session; HttpOnly")
                self.send_header("Set-Cookie", "out_of_scope=unused; Path=/other")
            payload = b'{"ok":true}'
        except Exception as error:
            server.failures.append(str(error))
            self.send_response(500)
            payload = b'{"message":"fixture failure"}'
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)

    do_GET = do_POST = handle_api


def main():
    with http.server.ThreadingHTTPServer(("127.0.0.1", 0), Http) as server:
        server.connections, server.failures = [], []
        worker = threading.Thread(target=server.serve_forever, daemon=True)
        worker.start()
        try:
            process = subprocess.run(
                [sys.argv[1], f"http://127.0.0.1:{server.server_port}/api",
                 "ws://127.0.0.1:1/", "http_session"],
                capture_output=True, text=True, timeout=15,
            )
            assert process.returncode == 0, process.stdout + process.stderr
            assert not server.failures, server.failures
            assert len(server.connections) == 4, "Unexpected HTTP request count"
            print("HTTP session: reuse, cookies, method reset, credential and Client isolation passed")
        finally:
            server.shutdown()
            worker.join()


if __name__ == "__main__":
    main()
