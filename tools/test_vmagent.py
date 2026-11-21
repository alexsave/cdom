#!/usr/bin/env python3
"""test_vmagent.py: unit test of tools/vmagent.py with a fake guest client that speaks the agent's protocol
(raw HTTP/1.0 GETs, percent-escaping every non-alphanumeric byte like agent.c does)."""
import os
import socket
import sys
import threading
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import vmagent  # noqa: E402
from http.server import ThreadingHTTPServer  # noqa: E402


def esc(s):
    return "".join(c if c.isascii() and c.isalnum() else "%%%02X" % b for c in s for b in c.encode("latin-1"))


def raw_get(port, path):
    s = socket.create_connection(("127.0.0.1", port), timeout=5)
    s.sendall(("GET %s HTTP/1.0\r\nHost: 10.0.2.2:8120\r\nConnection: close\r\n\r\n" % path).encode())
    d = b""
    while True:
        c = s.recv(4096)
        if not c:
            break
        d += c
    s.close()
    head, _, body = d.partition(b"\r\n\r\n")
    return body.decode("latin-1")


class T(unittest.TestCase):
    def setUp(self):
        self.q = vmagent.Queue()
        self.srv = ThreadingHTTPServer(("127.0.0.1", 0), vmagent.make_handler(self.q))
        self.srv.daemon_threads = True
        self.port = self.srv.server_address[1]
        threading.Thread(target=self.srv.serve_forever, daemon=True).start()

    def tearDown(self):
        self.srv.shutdown()
        self.srv.server_close()

    def guest(self, answers):
        """fake guest: polls /cmd until it has handled len(answers) commands, answering via /result"""
        seen = []

        def run():
            while len(seen) < len(answers):
                b = raw_get(self.port, "/cmd")
                if not b:
                    continue
                i, _, cmd = b.partition(" ")
                seen.append(cmd)
                raw_get(self.port, "/result?id=%s&text=%s" % (i, esc(answers[len(seen) - 1](cmd))))

        t = threading.Thread(target=run, daemon=True)
        t.start()
        return t, seen

    def test_idle_poll_is_empty(self):
        self.assertEqual(raw_get(self.port, "/cmd"), "")
        self.assertIn("last poll", raw_get(self.port, "/status"))

    def test_send_roundtrip_and_order(self):
        t, seen = self.guest([lambda c: "pong", lambda c: "ok pid=42"])
        code, text = vmagent.call(self.port, "/send?cmd=ping&wait=5")
        self.assertEqual((code, text), (200, "pong"))
        cmd = r"run C:\NS202\Program\netscape.exe http://10.0.2.2:8110/x?a=1&b=2"
        code, text = vmagent.call(self.port, "/send?" + vmagent.urllib.parse.urlencode({"cmd": cmd, "wait": 5}))
        self.assertEqual((code, text), (200, "ok pid=42"))
        t.join(5)
        self.assertEqual(seen, ["ping", cmd])

    def test_result_escaping_with_newlines_and_symbols(self):
        text = "0 [System Process]\n4 System\n812 netscape.exe\n100%&=+ \u00e9"
        t, _ = self.guest([lambda c: text])
        code, got = vmagent.call(self.port, "/send?cmd=ps&wait=5")
        self.assertEqual((code, got), (200, text))

    def test_timeout_without_guest(self):
        code, text = vmagent.call(self.port, "/send?cmd=ping&wait=0.3")
        self.assertEqual(code, 504)
        self.assertIn("timeout", text)

    def test_bad_requests(self):
        self.assertEqual(vmagent.call(self.port, "/send?cmd=&wait=1")[0], 400)
        self.assertEqual(vmagent.call(self.port, "/result?id=x&text=a")[0], 400)
        self.assertEqual(vmagent.call(self.port, "/nope")[0], 404)

    def test_each_command_handed_out_once(self):
        i = self.q.submit("ping")
        self.assertEqual(raw_get(self.port, "/cmd"), "%d ping" % i)
        self.assertEqual(raw_get(self.port, "/cmd"), "")


if __name__ == "__main__":
    unittest.main()
