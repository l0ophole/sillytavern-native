#!/usr/bin/env python3
"""OpenAI-compatible mock server used by the C04 integration tests."""
import json
import sys
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer


STREAM_CHUNKS = (
    'data: {"choices":[{"delta":{"content":"hello"}}]}\n\n'
    'data: {"choices":[{"delta":{"content":" world"}}]}\n\n'
)


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def do_POST(self):
        length = int(self.headers.get("Content-Length", "0"))
        raw = self.rfile.read(length)
        try:
            payload = json.loads(raw or b"{}")
        except ValueError:
            payload = {}

        if self.path.startswith("/error/"):
            self._json(401, {"error": {"message": "bad key"}})
            return

        if self.path.startswith("/malformed/"):
            body = b"data: {not json}\n\n"
            self.send_response(200)
            self.send_header("Content-Type", "text/event-stream")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self._write(body)
            return

        if self.path.startswith("/stream-error/"):
            body = (
                b'data: {"choices":[{"delta":{"content":"hello"}}]}\n\n'
                b'data: {"error":{"message":"boom"}}\n\n'
            )
            self.send_response(200)
            self.send_header("Content-Type", "text/event-stream")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self._write(body)
            return

        if self.path.startswith("/partial/"):
            body = (
                b'data: {"choices":[{"delta":{"content":"hello"}}]}\n\n'
                b"data: {not json}\n\n"
            )
            self.send_response(200)
            self.send_header("Content-Type", "text/event-stream")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self._write(body)
            return

        if self.path.startswith("/slow-partial/"):
            self.send_response(200)
            self.send_header("Content-Type", "text/event-stream")
            self.send_header("Cache-Control", "no-cache")
            self.send_header("Connection", "close")
            self.end_headers()
            self._write(b'data: {"choices":[{"delta":{"content":"partial"}}]}\n\n')
            time.sleep(2)
            self._write(b'data: {"choices":[{"delta":{"content":" tail"}}]}\n\ndata: [DONE]\n\n')
            return

        if self.path.startswith("/slow/"):
            self.send_response(200)
            self.send_header("Content-Type", "text/event-stream")
            self.send_header("Connection", "close")
            self.end_headers()
            self.wfile.flush()
            time.sleep(2)
            self._write(b'data: {"choices":[{"delta":{"content":"late"}}]}\n\ndata: [DONE]\n\n')
            return

        if payload.get("stream"):
            self.send_response(200)
            self.send_header("Content-Type", "text/event-stream")
            self.send_header("Cache-Control", "no-cache")
            self.send_header("Connection", "close")
            self.end_headers()
            self._write(STREAM_CHUNKS.encode())
            self._write(b"data: [DONE]\n\n")
            return

        self._json(200, {"choices": [{"message": {"role": "assistant", "content": "hello"}}]})

    def _json(self, status, value):
        body = json.dumps(value).encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self._write(body)

    def _write(self, body):
        try:
            self.wfile.write(body)
            self.wfile.flush()
        except (BrokenPipeError, ConnectionResetError):
            pass

    def log_message(self, fmt, *args):
        pass


def main():
    server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    with open(sys.argv[1], "w", encoding="utf-8") as handle:
        handle.write(str(server.server_port))
    server.serve_forever()


if __name__ == "__main__":
    main()
