#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Serves this directory with the two headers that make a page cross-origin isolated, which shared memory needs:
   python3 serve.py [port]   (default 8017), then open http://localhost:8017/"""
import http.server
import sys


class Handler(http.server.SimpleHTTPRequestHandler):
    extensions_map = {**http.server.SimpleHTTPRequestHandler.extensions_map, ".wasm": "application/wasm",
                      ".js": "text/javascript"}

    def end_headers(self):
        self.send_header("Cross-Origin-Opener-Policy", "same-origin")
        self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
        self.send_header("Cache-Control", "no-store")
        super().end_headers()


if __name__ == "__main__":
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8017
    http.server.ThreadingHTTPServer(("127.0.0.1", port), Handler).serve_forever()
