#!/usr/bin/env python3
"""Serves a directory to the XP guest's old browsers (docs/frames-design.md, section 15): inside the VM it is
http://10.0.2.2:<port>/.  Usage: vmserve.py <dir> <port>
Scripts go out as application/x-javascript, the type Netscape 3 needs for a SCRIPT SRC (it offers text/javascript as a
file to download: "Unknown File Type"); Netscape 2 has no SRC, Netscape 4 and IE 4 take either.  Every answer is
no-store, so a rebuilt file is fetched again, and every request is logged to stdout: the guest's pages report by image
requests to /mark/<tag>/..., which the log then holds (they answer 404, which an image request does not mind)."""
import functools
import http.server
import sys


class Handler(http.server.SimpleHTTPRequestHandler):
    # /r<digits>/ before a path is dropped: a run's own URLs (vmprobe.sh gives each run one), since Netscape 3.04 keeps a
    # hung load's URL poisoned after the browser is killed, and the next run of that URL hangs as well (docs/frames-design.md 15).
    def translate_path(self, path):
        if path.startswith("/r") and "/" in path[1:] and path[2:path.index("/", 1)].isdigit():
            path = path[path.index("/", 1):]
        return super().translate_path(path)

    extensions_map = {**http.server.SimpleHTTPRequestHandler.extensions_map,
                      ".js": "application/x-javascript", ".wasm": "application/wasm", ".html": "text/html; charset=utf-8"}

    def end_headers(self):
        self.send_header("Cache-Control", "no-store")
        super().end_headers()


root, port = sys.argv[1], int(sys.argv[2])
print(f"serving {root} on http://127.0.0.1:{port}/ (in the guest: http://10.0.2.2:{port}/)", flush=True)
http.server.ThreadingHTTPServer(("127.0.0.1", port), functools.partial(Handler, directory=root)).serve_forever()
