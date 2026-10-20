# SPDX-License-Identifier: LGPL-3.0-or-later
# Copyright (C) 2026 Cavanshirpro
# Licensed under LGPL-3.0-or-later; see NOTICE, LICENSE and COPYING.
"""Verify real class/method/field documentation returned by clangd's LSP hover."""
import argparse
import json
from pathlib import Path
import queue
import subprocess
import tempfile
import threading
import time

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--clangd", default="clangd")
    parser.add_argument("--build-dir", type=Path, default=ROOT / "build" / "default")
    parser.add_argument("--query-driver", help="Trusted compiler path/glob for standard include discovery")
    args = parser.parse_args()
    build = args.build_dir.resolve()
    if not (build / "compile_commands.json").is_file():
        raise RuntimeError("Configure CMake with CMAKE_EXPORT_COMPILE_COMMANDS=ON first")
    source = ROOT / "tests" / "hover.cpp"
    text = source.read_text(encoding="utf-8")
    command = [args.clangd, f"--compile-commands-dir={build}", "--background-index=false", "--log=error"]
    if args.query_driver:
        command.append(f"--query-driver={args.query_driver}")
    inbox = queue.Queue()
    with tempfile.TemporaryFile() as errors:
        process = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=errors)

        def reader():
            try:
                while True:
                    headers = {}
                    while True:
                        line = process.stdout.readline()
                        if not line:
                            raise EOFError("clangd closed its output")
                        if line in (b"\r\n", b"\n"):
                            break
                        key, value = line.decode().split(":", 1)
                        headers[key.lower()] = value.strip()
                    payload = process.stdout.read(int(headers["content-length"]))
                    inbox.put(json.loads(payload))
            except Exception as error:
                inbox.put(error)

        threading.Thread(target=reader, daemon=True).start()
        serial = 0

        def send(method, params=None, identifier=None):
            message = {"jsonrpc": "2.0", "method": method}
            if params is not None:
                message["params"] = params
            if identifier is not None:
                message["id"] = identifier
            body = json.dumps(message).encode()
            process.stdin.write(f"Content-Length: {len(body)}\r\n\r\n".encode() + body)
            process.stdin.flush()

        def request(method, params=None):
            nonlocal serial
            serial += 1
            send(method, params, serial)
            deadline = time.monotonic() + 30
            while True:
                item = inbox.get(timeout=max(0.01, deadline - time.monotonic()))
                if isinstance(item, Exception):
                    raise item
                if item.get("method") == "textDocument/publishDiagnostics" and item.get("params", {}).get("uri", "").casefold() == source.as_uri().casefold():
                    failures = [d["message"] for d in item.get("params", {}).get("diagnostics", []) if d.get("severity") == 1]
                    if failures:
                        raise RuntimeError("clangd diagnostics: " + "; ".join(failures))
                if item.get("id") == serial:
                    if "error" in item:
                        raise RuntimeError(str(item["error"]))
                    return item.get("result")
                if time.monotonic() >= deadline:
                    raise TimeoutError("clangd request timed out")

        try:
            request("initialize", {"processId": None, "rootUri": ROOT.as_uri(), "capabilities": {"textDocument": {"hover": {"contentFormat": ["markdown", "plaintext"]}}}})
            send("initialized", {})
            for header in sorted((ROOT / "include" / "discord").glob("*.hpp")):
                send("textDocument/didOpen", {"textDocument": {"uri": header.as_uri(), "languageId": "cpp", "version": 1, "text": header.read_text(encoding="utf-8")}})
                request("textDocument/documentSymbol", {"textDocument": {"uri": header.as_uri()}})
            send("textDocument/didOpen", {"textDocument": {"uri": source.as_uri(), "languageId": "cpp", "version": 1, "text": text}})
            # Wait for the initial AST/preamble before asking for header documentation.
            while True:
                item = inbox.get(timeout=30)
                if isinstance(item, Exception):
                    raise item
                if item.get("method") == "textDocument/publishDiagnostics":
                    failures = [d["message"] for d in item.get("params", {}).get("diagnostics", []) if d.get("severity") == 1]
                    if failures:
                        raise RuntimeError("clangd diagnostics: " + "; ".join(failures))
                    break
            count = 0
            missing = []
            for number, line in enumerate(text.splitlines()):
                if "// hover:" not in line:
                    continue
                code, marker = line.split("// hover:", 1)
                token, expected = marker.split("|", 1)
                position = code.rfind(token) + len(token) // 2
                result = request("textDocument/hover", {"textDocument": {"uri": source.as_uri()}, "position": {"line": number, "character": position}})
                rendered = json.dumps(result, ensure_ascii=True) if result else ""
                if expected.casefold() not in rendered.casefold():
                    missing.append(token)
                    print(f"Missing hover documentation for {token}: expected {expected!r}; got {rendered[:800]}")
                count += 1
            request("shutdown")
            send("exit")
            process.wait(timeout=5)
            if missing:
                raise RuntimeError("Missing hover documentation: " + ", ".join(missing))
            print(f"Verified {count} real clangd hovers for public types, methods and fields")
        except Exception:
            errors.seek(0)
            detail = errors.read().decode(errors="replace").strip()
            if detail:
                print(detail[:8000])
            raise
        finally:
            if process.poll() is None:
                process.kill()
                process.wait()
            process.stdin.close()
            process.stdout.close()


if __name__ == "__main__":
    main()
