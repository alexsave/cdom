/* httpd: see httpd.h. One poll() loop; each connection reads one request at a
 * time, answers it, then parses whatever pipelined bytes follow. */
#include "httpd.h"
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define MAX_CONNS 512
#define MAX_ROUTES 64
#define IDLE_SECONDS 60

struct httpd_res {
  int status;
  const char *ctype;
  char *body;
  size_t len, cap;
};

typedef struct {
  int fd;
  char *in;           /* request bytes received and not yet consumed */
  size_t in_len, in_cap;
  char *out;          /* response bytes not yet sent */
  size_t out_len, out_off;
  int close_after;    /* close once out is sent */
  time_t seen;
} conn;

static struct { const char *method, *path; httpd_handler *h; } routes[MAX_ROUTES];
static uint32_t nroutes;
static const char *static_root;

int httpd_route(const char *method, const char *path, httpd_handler *h) {
  if (nroutes == MAX_ROUTES) return -1;
  routes[nroutes].method = method;
  routes[nroutes].path = path;
  routes[nroutes].h = h;
  nroutes++;
  return 0;
}

static int lower(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }

static int ieq(const char *a, const char *b) {
  while (*a && lower(*a) == lower(*b)) a++, b++;
  return !*a && !*b;
}

const char *httpd_header(const httpd_req *q, const char *name) {
  for (uint32_t i = 0; i < q->nheaders; i++)
    if (ieq(q->headers[i].name, name)) return q->headers[i].value;
  return 0;
}

const char *httpd_query(const httpd_req *q, const char *key, uint32_t *len) {
  size_t kl = strlen(key);
  for (const char *p = q->query; *p;) {
    const char *e = strchr(p, '&');
    if (!e) e = p + strlen(p);
    if ((size_t)(e - p) >= kl && !memcmp(p, key, kl) && (p[kl] == '=' || p + kl == e)) {
      const char *v = p + kl + (p[kl] == '=');
      if (len) *len = (uint32_t)(e - v);
      return v;
    }
    p = *e ? e + 1 : e;
  }
  return 0;
}

/* ---- responses ---- */
static void oom(void) {
  fprintf(stderr, "httpd: out of memory\n");
  abort();
}

void httpd_status(httpd_res *r, int status, const char *content_type) {
  r->status = status;
  r->ctype = content_type;
}

char *httpd_room(httpd_res *r, size_t n) {
  if (r->len + n > r->cap) {
    size_t c = r->cap ? r->cap : 4096;
    while (c < r->len + n) c *= 2;
    r->body = realloc(r->body, c);
    if (!r->body) oom();
    r->cap = c;
  }
  r->len += n;
  return r->body + r->len - n;
}

void httpd_shrink(httpd_res *r, size_t unused) { r->len -= unused < r->len ? unused : r->len; }

void httpd_write(httpd_res *r, const void *p, size_t n) { memcpy(httpd_room(r, n), p, n); }

static const char *reason(int s) {
  switch (s) {
  case 200: return "OK";
  case 204: return "No Content";
  case 400: return "Bad Request";
  case 403: return "Forbidden";
  case 404: return "Not Found";
  case 405: return "Method Not Allowed";
  case 408: return "Request Timeout";
  case 413: return "Content Too Large";
  case 431: return "Request Header Fields Too Large";
  case 500: return "Internal Server Error";
  case 501: return "Not Implemented";
  case 503: return "Service Unavailable";
  default: return "Status";
  }
}

/* Queue the response on the connection (headers, then the body unless HEAD). */
static void queue(conn *c, httpd_res *r, int head_only) {
  char h[512];
  int n = snprintf(h, sizeof h,
                   "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\n"
                   "Cache-Control: no-cache\r\nConnection: %s\r\n\r\n",
                   r->status, reason(r->status), r->ctype ? r->ctype : "text/plain", r->len,
                   c->close_after ? "close" : "keep-alive");
  size_t body = head_only ? 0 : r->len;
  c->out = malloc((size_t)n + body);
  if (!c->out) oom();
  memcpy(c->out, h, (size_t)n);
  if (body) memcpy(c->out + n, r->body, body);
  c->out_len = (size_t)n + body;
  c->out_off = 0;
}

static void error_res(conn *c, int status, int close) {
  httpd_res r = {status, "text/plain", 0, 0, 0};
  const char *t = reason(status);
  httpd_write(&r, t, strlen(t));
  httpd_write(&r, "\n", 1);
  if (close) c->close_after = 1;
  queue(c, &r, 0);
  free(r.body);
}

/* ---- static files ---- */
static const char *content_type(const char *path) {
  static const char *T[][2] = {
    {".html", "text/html; charset=utf-8"}, {".js", "text/javascript; charset=utf-8"},
    {".mjs", "text/javascript; charset=utf-8"}, {".wasm", "application/wasm"},
    {".css", "text/css; charset=utf-8"}, {".png", "image/png"}, {".svg", "image/svg+xml"},
    {".ico", "image/x-icon"}, {".woff", "font/woff"}, {".woff2", "font/woff2"},
    {".ttf", "font/ttf"}, {".eot", "application/vnd.ms-fontobject"},
    {".txt", "text/plain; charset=utf-8"}};
  const char *dot = strrchr(path, '.');
  if (dot && !strchr(dot, '/'))
    for (size_t i = 0; i < sizeof T / sizeof *T; i++)
      if (ieq(T[i][0], dot)) return T[i][1];
  return "application/octet-stream";
}

/* path is decoded and starts with '/'. Segments "." and ".." and empty inner
 * segments are refused, so the file is always under the root. */
static int serve_file(const char *path, httpd_res *r) {
  if (!static_root) return 404;
  for (const char *s = path; *s;) { /* s is at a '/' */
    const char *e = strchr(s + 1, '/');
    size_t n = e ? (size_t)(e - s - 1) : strlen(s + 1);
    if ((n == 1 && s[1] == '.') || (n == 2 && s[1] == '.' && s[2] == '.')) return 404;
    if (n == 0 && e) return 404;
    if (!e) break;
    s = e;
  }
  char full[4096];
  int n = snprintf(full, sizeof full, "%s%s%s", static_root, path,
                   path[strlen(path) - 1] == '/' ? "index.html" : "");
  if (n < 0 || (size_t)n >= sizeof full) return 404;
  int fd = open(full, O_RDONLY | O_NOFOLLOW);
  if (fd < 0) return 404;
  struct stat st;
  if (fstat(fd, &st) || !S_ISREG(st.st_mode)) {
    close(fd);
    return 404;
  }
  char *d = httpd_room(r, (size_t)st.st_size);
  size_t got = 0;
  while (got < (size_t)st.st_size) {
    ssize_t k = read(fd, d + got, (size_t)st.st_size - got);
    if (k <= 0) {
      if (k < 0 && errno == EINTR) continue;
      close(fd);
      return 500;
    }
    got += (size_t)k;
  }
  close(fd);
  httpd_status(r, 200, content_type(full));
  return 200;
}

/* ---- request parsing ---- */
static int hexv(int c) {
  if (c >= '0' && c <= '9') return c - '0';
  c = lower(c);
  return c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
}

static int tchar(int c) { /* RFC 9110 token */
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
         (c && strchr("!#$%&'*+-.^_`|~", c));
}

/* One line ending at "\r\n" or "\n" starting at p; NUL-terminates it in place and
 * returns the start of the next line, or 0 if no line end before end. */
static char *line(char *p, char *end) {
  char *nl = memchr(p, '\n', (size_t)(end - p));
  if (!nl) return 0;
  if (nl > p && nl[-1] == '\r') nl[-1] = 0;
  *nl = 0;
  return nl + 1;
}

/* Tries to take one request from c->in. Returns 0 when more bytes are needed
 * (c->in is untouched), 1 when a response was queued. The head is parsed in a
 * scratch copy, NUL-terminated in place; the body stays in c->in. */
static char head_copy[HTTPD_MAX_HEAD + 1];

static int handle(conn *c) {
  const char *in = c->in, *in_end = c->in + c->in_len;
  /* find the blank line that ends the head */
  size_t head_len = 0;
  for (const char *p = in; p < in_end && !head_len; p++) {
    if (*p != '\n') continue;
    if (p + 1 < in_end && p[1] == '\n') head_len = (size_t)(p + 2 - in);
    else if (p + 2 < in_end && p[1] == '\r' && p[2] == '\n') head_len = (size_t)(p + 3 - in);
  }
  if (!head_len) {
    if (c->in_len > HTTPD_MAX_HEAD) { error_res(c, 431, 1); return 1; }
    return 0;
  }
  if (head_len > HTTPD_MAX_HEAD) { error_res(c, 431, 1); return 1; }
  memcpy(head_copy, in, head_len);
  head_copy[head_len] = 0;
  char *buf = head_copy, *head_end = head_copy + head_len;

  /* request line: METHOD SP target SP HTTP/1.x */
  char *p = buf, *next = line(p, head_end);
  char *sp1 = strchr(p, ' '), *sp2 = sp1 ? strchr(sp1 + 1, ' ') : 0;
  if (!sp1 || !sp2 || strchr(sp2 + 1, ' ') || sp1 == p) { error_res(c, 400, 1); return 1; }
  *sp1 = *sp2 = 0;
  const char *method = p, *version = sp2 + 1;
  char *target = sp1 + 1;
  for (const char *m = method; *m; m++)
    if (!tchar(*m)) { error_res(c, 400, 1); return 1; }
  int v11 = !strcmp(version, "HTTP/1.1");
  if (!v11 && strcmp(version, "HTTP/1.0")) { error_res(c, 400, 1); return 1; }
  if (target[0] != '/') { error_res(c, 400, 1); return 1; }

  /* headers */
  httpd_hdr hs[HTTPD_MAX_HEADERS];
  uint32_t nh = 0;
  for (p = next; p < head_end;) {
    next = line(p, head_end);
    if (!*p) break; /* the blank line */
    char *colon = strchr(p, ':');
    if (!colon || colon == p) { error_res(c, 400, 1); return 1; }
    for (char *k = p; k < colon; k++)
      if (!tchar(*k)) { error_res(c, 400, 1); return 1; }
    if (nh == HTTPD_MAX_HEADERS) { error_res(c, 431, 1); return 1; }
    *colon = 0;
    char *v = colon + 1, *ve = v + strlen(v);
    while (*v == ' ' || *v == '\t') v++;
    while (ve > v && (ve[-1] == ' ' || ve[-1] == '\t')) *--ve = 0;
    hs[nh++] = (httpd_hdr){p, v};
    p = next;
  }
  httpd_req q = {method, 0, "", 0, 0, hs, nh};

  /* body: Content-Length only (no chunked request bodies) */
  size_t body_len = 0;
  int have_len = 0;
  for (uint32_t i = 0; i < nh; i++) {
    if (ieq(hs[i].name, "transfer-encoding")) { error_res(c, 501, 1); return 1; }
    if (!ieq(hs[i].name, "content-length")) continue;
    const char *d = hs[i].value;
    size_t v = 0;
    if (!*d) { error_res(c, 400, 1); return 1; }
    for (; *d; d++) {
      if (*d < '0' || *d > '9') { error_res(c, 400, 1); return 1; }
      v = v * 10 + (size_t)(*d - '0');
      if (v > HTTPD_MAX_BODY) { error_res(c, 413, 1); return 1; }
    }
    if (have_len && v != body_len) { error_res(c, 400, 1); return 1; }
    body_len = v;
    have_len = 1;
  }
  if ((size_t)(in_end - in) - head_len < body_len) return 0; /* the body is still arriving */
  q.body = in + head_len;
  q.body_len = (uint32_t)body_len;

  /* keep-alive */
  const char *conn_h = httpd_header(&q, "connection");
  c->close_after = v11 ? (conn_h && ieq(conn_h, "close")) : !(conn_h && ieq(conn_h, "keep-alive"));

  /* target: path and query; decode the path */
  char *qm = strchr(target, '?');
  if (qm) {
    *qm = 0;
    q.query = qm + 1;
  }
  char *w = target;
  for (char *r = target; *r; r++) {
    if (*r == '%') {
      int hi = hexv(r[1]), lo = hi < 0 ? -1 : hexv(r[2]);
      if (lo < 0 || (hi == 0 && lo == 0)) { error_res(c, 400, 1); return 1; }
      *w++ = (char)(hi * 16 + lo);
      r += 2;
    } else {
      *w++ = *r;
    }
  }
  *w = 0;
  q.path = target;

  /* dispatch */
  int head = !strcmp(method, "HEAD");
  const char *as = head ? "GET" : method;
  httpd_res r = {200, 0, 0, 0, 0};
  int path_known = 0, done = 0;
  for (uint32_t i = 0; i < nroutes && !done; i++) {
    if (strcmp(routes[i].path, q.path)) continue;
    path_known = 1;
    if (strcmp(routes[i].method, as)) continue;
    routes[i].h(&q, &r);
    done = 1;
  }
  if (!done) {
    int s = path_known ? 405 : !strcmp(as, "GET") ? serve_file(q.path, &r) : 404;
    if (s != 200) {
      free(r.body);
      error_res(c, s, 0);
    } else {
      queue(c, &r, head);
      free(r.body);
    }
  } else {
    queue(c, &r, head);
    free(r.body);
  }
  /* consume the request */
  size_t used = head_len + body_len;
  memmove(c->in, c->in + used, c->in_len - used);
  c->in_len -= used;
  return 1;
}

/* ---- the loop ---- */
static conn conns[MAX_CONNS];
static struct pollfd pfd[MAX_CONNS + 1];

static void drop(conn *c) {
  close(c->fd);
  free(c->in);
  free(c->out);
  memset(c, 0, sizeof *c);
  c->fd = -1;
}

/* Parse requests while no response is waiting. */
static void pump(conn *c) {
  while (!c->out && c->in_len && !c->close_after && handle(c)) {}
}

static int flush_out(conn *c) { /* 0 ok (maybe partial), -1 dead */
  while (c->out && c->out_off < c->out_len) {
    ssize_t k = send(c->fd, c->out + c->out_off, c->out_len - c->out_off, 0);
    if (k < 0) {
      if (errno == EINTR) continue;
      if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
      return -1;
    }
    c->out_off += (size_t)k;
  }
  if (c->out) {
    free(c->out);
    c->out = 0;
    c->out_len = c->out_off = 0;
    if (c->close_after) return -1;
  }
  return 0;
}

int httpd_serve(uint16_t port, const char *root) {
  static_root = root;
  signal(SIGPIPE, SIG_IGN);
  int ls = socket(AF_INET, SOCK_STREAM, 0);
  if (ls < 0) return -1;
  int one = 1;
  setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
  struct sockaddr_in a = {0};
  a.sin_family = AF_INET;
  a.sin_port = htons(port);
  a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if (bind(ls, (struct sockaddr *)&a, sizeof a) || listen(ls, 128)) {
    perror("httpd: bind");
    close(ls);
    return -1;
  }
  fcntl(ls, F_SETFL, O_NONBLOCK);
  for (int i = 0; i < MAX_CONNS; i++) conns[i].fd = -1;
  for (;;) {
    nfds_t n = 0;
    pfd[n++] = (struct pollfd){ls, POLLIN, 0};
    int map[MAX_CONNS];
    for (int i = 0; i < MAX_CONNS; i++) {
      if (conns[i].fd < 0) continue;
      map[n - 1] = i;
      pfd[n++] = (struct pollfd){conns[i].fd, conns[i].out ? POLLOUT : POLLIN, 0};
    }
    int k = poll(pfd, n, 1000);
    if (k < 0) {
      if (errno == EINTR) continue;
      perror("httpd: poll");
      return -1;
    }
    time_t now = time(0);
    for (nfds_t i = 1; i < n; i++) {
      conn *c = &conns[map[i - 1]];
      short re = pfd[i].revents;
      if (!re) {
        if (now - c->seen > IDLE_SECONDS) drop(c);
        continue;
      }
      c->seen = now;
      if (re & POLLIN) {
        if (c->in_cap - c->in_len < 16384) {
          size_t cap = c->in_cap ? c->in_cap * 2 : 32768;
          if (cap > HTTPD_MAX_HEAD + HTTPD_MAX_BODY + 65536) cap = HTTPD_MAX_HEAD + HTTPD_MAX_BODY + 65536;
          if (cap - c->in_len == 0) { drop(c); continue; } /* a client that sends past every limit */
          c->in = realloc(c->in, cap);
          if (!c->in) oom();
          c->in_cap = cap;
        }
        ssize_t got = recv(c->fd, c->in + c->in_len, c->in_cap - c->in_len, 0);
        if (got == 0 || (got < 0 && errno != EAGAIN && errno != EINTR && errno != EWOULDBLOCK)) {
          drop(c);
          continue;
        }
        if (got > 0) c->in_len += (size_t)got;
        pump(c);
      } else if (re & (POLLERR | POLLHUP | POLLNVAL)) {
        drop(c);
        continue;
      }
      /* send what is queued; then take any pipelined request */
      while (c->fd >= 0 && c->out) {
        if (flush_out(c)) { drop(c); break; }
        if (c->out) break; /* the socket is full: wait for POLLOUT */
        pump(c);
      }
    }
    if (pfd[0].revents & POLLIN) {
      for (;;) {
        int fd = accept(ls, 0, 0);
        if (fd < 0) break;
        int slot = -1;
        for (int i = 0; i < MAX_CONNS && slot < 0; i++)
          if (conns[i].fd < 0) slot = i;
        if (slot < 0) {
          close(fd);
          continue;
        }
        fcntl(fd, F_SETFL, O_NONBLOCK);
#ifdef SO_NOSIGPIPE
        setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
#endif
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
        conns[slot].fd = fd;
        conns[slot].seen = now;
      }
    }
  }
}
