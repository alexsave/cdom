#!/usr/bin/env python3
"""vmagent.py: the Mac side of the XP guest's remote-command agent (tools/vmagent/agent.c).

  vmagent.py serve [--port 8120] [--bind 127.0.0.1]   the queue server the guest polls
  vmagent.py send "<command>" [--wait 30]             queue one command, print its result text
  vmagent.py status                                   when the guest agent last polled

Commands: run <command line> | kill <image name> | ps | ping | exit.
The guest reaches this server at http://10.0.2.2:8120 (QEMU user networking maps it to the Mac's loopback).
Guest protocol: GET /cmd answers an empty body or "N command"; GET /result?id=N&text=<escaped> returns the text.
CLI protocol (same server, loopback only use): GET /send?cmd=<escaped>&wait=S blocks until the result or times out.
"""
import argparse
import sys
import threading
import time
import urllib.error
import urllib.parse
import urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer


class Queue:
    def __init__(self):
        self.cv = threading.Condition()
        self.pending = []      # (id, cmd) not yet handed to the guest
        self.results = {}      # id -> text
        self.next_id = 1
        self.last_poll = None  # time.time() of the guest's last GET /cmd

    def submit(self, cmd):
        with self.cv:
            i = self.next_id
            self.next_id += 1
            self.pending.append((i, cmd))
            return i

    def take(self):
        with self.cv:
            self.last_poll = time.time()
            return self.pending.pop(0) if self.pending else None

    def put_result(self, i, text):
        with self.cv:
            self.results[i] = text
            self.cv.notify_all()

    def wait(self, i, seconds):
        end = time.time() + seconds
        with self.cv:
            while i not in self.results:
                left = end - time.time()
                if left <= 0:
                    return None
                self.cv.wait(left)
            return self.results.pop(i)


def make_handler(q):
    class H(BaseHTTPRequestHandler):
        protocol_version = "HTTP/1.0"

        def log_message(self, *a):
            pass

        def reply(self, body, code=200):
            data = body.encode("utf-8", "replace")
            self.send_response(code)
            self.send_header("Content-Type", "text/plain; charset=utf-8")
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)

        def do_GET(self):
            u = urllib.parse.urlsplit(self.path)
            qs = urllib.parse.parse_qs(u.query, keep_blank_values=True, encoding="latin-1")
            if u.path == "/cmd":
                t = q.take()
                self.reply("" if t is None else "%d %s" % t)
            elif u.path == "/result":
                try:
                    i = int(qs["id"][0])
                except (KeyError, ValueError):
                    return self.reply("bad id\n", 400)
                q.put_result(i, qs.get("text", [""])[0])
                self.reply("ok")
            elif u.path == "/send":
                cmd = urllib.parse.parse_qs(u.query, encoding="utf-8").get("cmd", [""])[0]
                if not cmd or "\n" in cmd or "\r" in cmd:
                    return self.reply("a command is one non-empty line\n", 400)
                wait = float(qs.get("wait", ["30"])[0])
                i = q.submit(cmd)
                r = q.wait(i, wait)
                if r is None:
                    self.reply("timeout: no result for command %d in %gs (agent running? try: status)\n" % (i, wait), 504)
                else:
                    self.reply(r)
            elif u.path == "/status":
                lp = q.last_poll
                self.reply("never polled\n" if lp is None else "last poll %.1fs ago\n" % (time.time() - lp))
            else:
                self.reply("not found\n", 404)

    return H


def serve(port, bind):
    q = Queue()
    srv = ThreadingHTTPServer((bind, port), make_handler(q))
    srv.daemon_threads = True
    print("vmagent serving on %s:%d" % (bind, port), flush=True)
    srv.serve_forever()


def call(port, path):
    url = "http://127.0.0.1:%d%s" % (port, path)
    try:
        with urllib.request.urlopen(url, timeout=3600) as r:
            return r.status, r.read().decode("utf-8", "replace")
    except urllib.error.HTTPError as e:
        with e:
            return e.code, e.read().decode("utf-8", "replace")
    except urllib.error.URLError as e:
        return 0, "cannot reach the vmagent server on port %d (run: vmagent.py serve): %s\n" % (port, e.reason)


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", type=int, default=8120)
    sub = ap.add_subparsers(dest="mode", required=True)
    s = sub.add_parser("serve")
    s.add_argument("--bind", default="127.0.0.1")
    s = sub.add_parser("send")
    s.add_argument("cmd")
    s.add_argument("--wait", type=float, default=30)
    sub.add_parser("status")
    a = ap.parse_args(argv)
    if a.mode == "serve":
        serve(a.port, a.bind)
        return 0
    if a.mode == "send":
        code, text = call(a.port, "/send?" + urllib.parse.urlencode({"cmd": a.cmd, "wait": a.wait}))
    else:
        code, text = call(a.port, "/status")
    sys.stdout.write(text if text.endswith("\n") or not text else text + "\n")
    return 0 if code == 200 else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
