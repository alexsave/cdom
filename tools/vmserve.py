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
import time


class Server(http.server.ThreadingHTTPServer):
    daemon_threads = True
    request_queue_size = 128


class Handler(http.server.SimpleHTTPRequestHandler):
    timeout = 3600  # a connection a killed browser left open does not hold a thread for ever (Netscape 2 busy in a long layout opens its image connections late: 20 s closed them under it, "Connection reset by peer")
    # HTTP/1.1 with the Content-Length the base class sends: Netscape 3.04 asks for keep-alive, and a server that closes the
    # connection after each answer (HTTP/1.0) sometimes left its next request, the frame's page, unanswered for ever.
    protocol_version = "HTTP/1.1"
    # /r<digits>/ before a path is dropped: a run's own URLs (vmprobe.sh gives each run one), since Netscape 3.04 keeps a
    # hung load's URL poisoned after the browser is killed, and the next run of that URL hangs as well (docs/frames-design.md 15).
    def translate_path(self, path):
        if path.startswith("/r") and "/" in path[1:] and path[2:path.index("/", 1)].isdigit():
            path = path[path.index("/", 1):]
        return super().translate_path(path)

    extensions_map = {**http.server.SimpleHTTPRequestHandler.extensions_map,
                      ".js": "application/x-javascript", ".wasm": "application/wasm", ".html": "text/html; charset=utf-8"}

    # The log's time has milliseconds: a probe's wall time is read from the gap between two requests (docs/descent.md, "Netscape 2's limits").
    def log_date_time_string(self):
        t = time.time()
        return time.strftime("%d/%b/%Y %H:%M:%S", time.localtime(t)) + ".%03d" % (int(t * 1000) % 1000)

    def end_headers(self):
        self.send_header("Cache-Control", "no-store")
        # Netscape 2 asks for keep-alive but does not end an answer at its Content-Length: it waits for the close, so with
        # HTTP/1.1 every batch of its 4 connections sat out the 20 s timeout above (a page of 700 images took 160 s, found by
        # the framebuffer probes, docs/descent.md).  Its answers say close, and the connection is closed after each.
        if self.headers.get("User-Agent", "").startswith("Mozilla/2"):
            self.send_header("Connection", "close")
            self.close_connection = True
        super().end_headers()


root, port = sys.argv[1], int(sys.argv[2])
print(f"serving {root} on http://127.0.0.1:{port}/ (in the guest: http://10.0.2.2:{port}/)", flush=True)
Server(("127.0.0.1", port), functools.partial(Handler, directory=root)).serve_forever()
