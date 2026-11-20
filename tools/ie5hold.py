#!/usr/bin/env python3
"""The IE 5 test hold (docs/ie5-design.md): accepts connections on 127.0.0.1:8096 and never answers them.
The Utilu standalone IE 5.01 and IE 5.5 builds crash natively when any page, even one with no script, reaches Done on
the XP VM; a page that still waits on a resource never does, so its scripts can run.
For a test only: add <img src="http://10.0.2.2:8096/x.gif" width="1" height="1"> to a copy of a page in tests/out.
tests/build.sh refuses a shipped or built file that names the port.
Usage: python3 tools/ie5hold.py   (stop it with ctrl-c)"""
import socket

s = socket.socket()
s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
s.bind(("127.0.0.1", 8096))
s.listen(64)
held = []
while True:
    c, _ = s.accept()
    held.append(c)
