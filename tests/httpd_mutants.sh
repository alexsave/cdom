#!/bin/bash
# Mutation checks for tests/httpd.mjs: each mutant of src/httpd.c must fail the test.
set -u
cd "$(dirname "$0")/.."
m() { tools/mutate.sh src/httpd.c "$1" "$2" node tests/httpd.mjs || bad=1; }
bad=0
m 's/\(n == 2 && s\[1\] == \x27.\x27 && s\[2\] == \x27.\x27\)/0/' 'FAIL traversal /../secret.txt'
m 's/if \(c->in_len > HTTPD_MAX_HEAD\) \{ error_res\(c, 431, 1\); return 1; \}/{}/' 'FAIL oversized unfinished header'
m 's/size_t body = head_only \? 0 : r->len;/size_t body = head_only * 0 + r->len;/' 'FAIL HEAD sends no body'
m 's/(if \(c->out\) break; \/\* the socket is full: wait for POLLOUT \*\/\n\s*)pump\(c\);/$1/' 'FAIL pipelined keep-alive in order'
m 's/"application\/wasm"/"application\/octet-stream"/' 'FAIL wasm bytes and type'
m 's/< body_len\) return 0;/< 0) return 0;/' 'FAIL body split across writes'
m 's/\(conn_h && ieq\(conn_h, "close"\)\)/0/' 'FAIL Connection: close closes'
m 's/if \(v > HTTPD_MAX_BODY\) \{ error_res\(c, 413, 1\); return 1; \}//' 'FAIL Content-Length over the limit'
m 's/if \(lo < 0 \|\| \(hi == 0 && lo == 0\)\)/if (lo < 0)/' 'FAIL encoded NUL is 400'
m 's/path_known \? 405 :/path_known * 0 ? 405 :/' 'FAIL wrong method is 405'
m 's/if \(nh == HTTPD_MAX_HEADERS\) \{ error_res\(c, 431, 1\); return 1; \}/if (nh == HTTPD_MAX_HEADERS) break;/' 'FAIL too many headers'
exit $bad
