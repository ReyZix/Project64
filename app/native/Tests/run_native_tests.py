#!/usr/bin/env python3
"""Run native service tests against a local, disposable stdlib HTTP/WebSocket fixture."""
import argparse
import base64
import hashlib
import json
import socket
import struct
import subprocess
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlsplit


class Fixture(ThreadingHTTPServer):
    daemon_threads = True

    def __init__(self):
        super().__init__(("127.0.0.1", 0), Handler)
        self.lock = threading.Lock()
        self.stats = {"wifiMoves": [], "wifiCommands": [], "wsMoves": [], "httpMoves": [],
                      "accountConcurrent": 0, "maxConcurrent": 0}


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, *_args):
        pass

    def reply(self, code, obj):
        body = json.dumps(obj, ensure_ascii=False).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def frame(self, opcode, payload=b""):
        n = len(payload)
        size = bytes([n]) if n < 126 else b"\x7e" + struct.pack("!H", n)
        self.wfile.write(bytes([0x80 | opcode]) + size + payload)
        self.wfile.flush()

    def exact(self, n):
        data = b""
        while len(data) < n:
            chunk = self.rfile.read(n - len(data))
            if not chunk:
                raise EOFError()
            data += chunk
        return data

    def websocket(self):
        key = self.headers.get("Sec-WebSocket-Key")
        if not key:
            self.reply(400, {"error": "Missing WebSocket key"})
            return
        accept = base64.b64encode(hashlib.sha1((key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11").encode()).digest()).decode()
        self.send_response(101)
        self.send_header("Upgrade", "websocket")
        self.send_header("Connection", "Upgrade")
        self.send_header("Sec-WebSocket-Accept", accept)
        self.end_headers()
        self.connection.settimeout(5)
        for obj in ({"type": "board", "data": "e2e4"}, {"type": "status", "data": 0}):
            self.frame(1, json.dumps(obj).encode())
        while True:
            head = self.exact(2)
            opcode, masked, size = head[0] & 15, head[1] & 128, head[1] & 127
            if size == 126:
                size = struct.unpack("!H", self.exact(2))[0]
            elif size == 127:
                size = struct.unpack("!Q", self.exact(8))[0]
            if size > 65536:
                return
            mask = self.exact(4) if masked else b""
            data = self.exact(size)
            if mask:
                data = bytes(c ^ mask[i % 4] for i, c in enumerate(data))
            if opcode == 8:
                self.frame(8, data)
                return
            if opcode == 9:
                self.frame(10, data)
                continue
            if opcode != 1:
                continue
            obj = json.loads(data)
            with self.server.lock:
                if obj.get("type") == "move":
                    self.server.stats["wifiMoves"].append(obj.get("data"))
                    self.server.stats["wsMoves"].append(obj.get("data"))
                elif obj.get("type") == "cmd":
                    self.server.stats["wifiCommands"].append(obj.get("data"))
            if obj.get("type") == "cmd" and obj.get("data") == 3:
                self.frame(8, struct.pack("!H", 1000))
                return

    def stream(self, chunks=None):
        self.send_response(200)
        self.send_header("Content-Type", "application/x-ndjson")
        if chunks is not None:
            self.send_header("Content-Length", str(sum(map(len, chunks))))
        else:
            self.send_header("Connection", "close")
            self.close_connection = True
        self.end_headers()
        if chunks is not None:
            for chunk in chunks:
                self.wfile.write(chunk)
                self.wfile.flush()
                time.sleep(0.02)
        else:
            while True:
                self.wfile.write(b"\n")
                self.wfile.flush()
                time.sleep(0.2)

    def do_GET(self):
        try:
            path = urlsplit(self.path).path
            if path == "/ws":
                self.websocket()
            elif path == "/test/stats":
                with self.server.lock:
                    stats = dict(self.server.stats)
                self.reply(200, stats)
            elif path == "/api/account":
                token = self.headers.get("Authorization", "").removeprefix("Bearer ")
                with self.server.lock:
                    self.server.stats["accountConcurrent"] += 1
                    self.server.stats["maxConcurrent"] = max(self.server.stats["maxConcurrent"], self.server.stats["accountConcurrent"])
                try:
                    if token == "slow":
                        time.sleep(0.1)
                    code = {"bad": 401, "limited": 429, "error": 500}.get(token, 200)
                    self.reply(code, {"id": "tester", "username": "Tester"} if code == 200 else {"error": "Fixture error"})
                finally:
                    with self.server.lock:
                        self.server.stats["accountConcurrent"] -= 1
            elif path == "/api/board/game/stream/ndjson":
                self.stream([b'\nnot json\n{"text":"caf\xc3', b'\xa9"}\n', b'{"final":true}'])
            elif path.endswith("/slow") or path == "/api/stream/event":
                self.stream()
            elif path.startswith("/api/board/game/stream/"):
                game = {"type": "gameFull", "id": path.rsplit("/", 1)[-1], "variant": {"key": "standard"},
                        "initialFen": "startpos", "white": {"id": "tester", "name": "Tester"},
                        "black": {"id": "opponent", "name": "Opponent"},
                        "state": {"type": "gameState", "moves": "", "status": "started", "wtime": 600000, "btime": 600000}}
                self.send_response(200)
                self.send_header("Content-Type", "application/x-ndjson")
                self.send_header("Connection", "close")
                self.end_headers()
                self.close_connection = True
                self.wfile.write(json.dumps(game).encode() + b"\n")
                self.wfile.flush()
                while True:
                    time.sleep(0.2)
                    self.wfile.write(b"\n")
                    self.wfile.flush()
            else:
                self.reply(404, {"error": "Unknown fixture route"})
        except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError, EOFError, socket.timeout):
            pass

    def do_POST(self):
        try:
            path = urlsplit(self.path).path
            length = int(self.headers.get("Content-Length", "0"))
            body = self.rfile.read(length)
            if path == "/api":
                obj = json.loads(body)
                with self.server.lock:
                    if obj.get("type") == "move":
                        self.server.stats["wifiMoves"].append(obj.get("data"))
                        self.server.stats["httpMoves"].append(obj.get("data"))
                    elif obj.get("type") == "cmd":
                        self.server.stats["wifiCommands"].append(obj.get("data"))
                self.reply(200, {"ok": True})
            elif path == "/api/board/game/g1/move/d2d4":
                self.reply(400, {"error": "Rejected test move"})
            else:
                self.reply(200, {"ok": True})
        except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError, EOFError, socket.timeout):
            pass


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--hardware")
    parser.add_argument("--online")
    parser.add_argument("--session")
    parser.add_argument("--networks")
    parser.add_argument("--simulator")
    parser.add_argument("--ui", help="built UISmokeTests.app; requires --simulator")
    args = parser.parse_args()
    if args.session and not args.networks:
        parser.error("--session requires --networks")
    if args.ui:
        if not args.simulator:
            parser.error("--ui requires --simulator")
        subprocess.run(["xcrun", "simctl", "install", args.simulator, args.ui], check=True, timeout=30)
        result = subprocess.run(["xcrun", "simctl", "launch", "--console", "--terminate-running-process",
                                 args.simulator, "edu.project64.native.ui-tests"],
                                capture_output=True, text=True, timeout=30)
        print(result.stdout, end="")
        print(result.stderr, end="")
        if result.returncode or "Native UIKit and isolated Keychain smoke tests passed" not in result.stdout:
            raise SystemExit("UIKit/Keychain smoke test failed")
    server = Fixture()
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    host = f"127.0.0.1:{server.server_port}"
    base = f"http://{host}"
    prefix = ["xcrun", "simctl", "spawn", args.simulator] if args.simulator else []
    try:
        for name, executable, extra in (("hardware", args.hardware, [host]),
                                         ("online", args.online, [base]),
                                         ("session", args.session, [base, args.networks])):
            if executable:
                print(f"Running {name} tests", flush=True)
                subprocess.run(prefix + [executable] + extra, check=True, timeout=45)
    finally:
        server.shutdown()
        server.server_close()
        thread.join()


if __name__ == "__main__":
    main()
