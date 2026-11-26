/* agent.c: the remote-command agent for the Windows XP guest (32-bit, no C runtime, no Windows SDK headers).
 * It polls GET http://10.0.2.2:8120/cmd about once a second (10.0.2.2 is the Mac in QEMU user networking).
 * The reply body is empty (nothing to do) or one line "N command".  Commands:
 *   run <command line>   CreateProcess with that command line     -> "ok pid=P" or "err E"
 *   kill <image name>    terminate every process of that name      -> "killed K" or "none"
 *   ps                   one line "pid name" per process
 *   ping                 -> "pong"
 *   exit                 ends the agent
 * The result goes back as GET /result?id=N&text=<percent-escaped>.
 * Built by build.sh (clang + lld-link, own prototypes, import libs made with llvm-dlltool). */

typedef unsigned long DWORD;
typedef unsigned int UINT;
typedef int BOOL;
typedef void *HANDLE;
typedef unsigned short WORD;
typedef unsigned char BYTE;
#define WINAPI __stdcall
#define IMP __declspec(dllimport) WINAPI

typedef struct {
    DWORD cb; char *r0, *desktop, *title;
    DWORD x, y, xs, ys, xc, yc, fill, flags;
    WORD show, r1; BYTE *r2; HANDLE in, out, err;
} SI;
typedef struct { HANDLE proc, thread; DWORD pid, tid; } PI;
typedef struct {
    DWORD size, usage, pid, heap, module, threads, ppid; long pri; DWORD flags; char exe[260];
} PE;
typedef struct { short family; unsigned short port; DWORD addr; char zero[8]; } SA;

BOOL IMP CreateProcessA(const char *, char *, void *, void *, BOOL, DWORD, void *, const char *, SI *, PI *);
BOOL IMP CloseHandle(HANDLE);
void IMP Sleep(DWORD);
void IMP ExitProcess(UINT);
DWORD IMP GetLastError(void);
DWORD IMP GetCurrentProcessId(void);
HANDLE IMP CreateToolhelp32Snapshot(DWORD, DWORD);
BOOL IMP Process32First(HANDLE, PE *);
BOOL IMP Process32Next(HANDLE, PE *);
HANDLE IMP OpenProcess(DWORD, BOOL, DWORD);
BOOL IMP TerminateProcess(HANDLE, UINT);
int IMP WSAStartup(WORD, void *);
UINT IMP socket(int, int, int);
int IMP connect(UINT, const SA *, int);
int IMP send(UINT, const char *, int, int);
int IMP recv(UINT, char *, int, int);
int IMP setsockopt(UINT, int, int, const char *, int);
int IMP closesocket(UINT);

void *memset(void *p, int c, unsigned n) {
    volatile char *q = (volatile char *)p;
    while (n--) *q++ = (char)c;
    return p;
}

#define HOST "10.0.2.2:8120"
#define MAXTEXT 3000

static char rq[16384];   /* the request */
static char rp[4096];    /* the reply */
static char out[MAXTEXT + 64];
static int outn;

static int app(char *d, int n, int cap, const char *s) {
    while (*s && n < cap - 1) d[n++] = *s++;
    d[n] = 0;
    return n;
}

static int appnum(char *d, int n, int cap, DWORD v) {
    char t[12]; int k = 0;
    if (!v) t[k++] = '0';
    while (v) { t[k++] = (char)('0' + v % 10); v /= 10; }
    while (k && n < cap - 1) d[n++] = t[--k];
    d[n] = 0;
    return n;
}

static void put(const char *s) { outn = app(out, outn, MAXTEXT, s); }
static void putnum(DWORD v) { outn = appnum(out, outn, MAXTEXT, v); }

static int lower(int c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }

static int ieq(const char *a, const char *b) {
    while (*a && *b) { if (lower(*a) != lower(*b)) return 0; a++; b++; }
    return *a == *b;
}

static int starts(const char *s, const char *p) {
    while (*p) { if (*s++ != *p++) return 0; }
    return 1;
}

/* one HTTP/1.0 GET to the Mac; the body of the reply is left in rp (NUL ended), returns 1 when a reply came */
static int get(const char *path) {
    UINT s; SA a; int n, got = 0, i; DWORD to = 5000;
    n = app(rq, 0, sizeof rq, "GET ");
    n = app(rq, n, sizeof rq, path);
    n = app(rq, n, sizeof rq, " HTTP/1.0\r\nHost: " HOST "\r\nConnection: close\r\n\r\n");
    rp[0] = 0;
    s = socket(2, 1, 6);
    if (s == (UINT)~0) return 0;
    setsockopt(s, 0xffff, 0x1006, (const char *)&to, 4);
    memset(&a, 0, sizeof a);
    a.family = 2; a.port = (unsigned short)((8120 >> 8) | ((8120 & 255) << 8));
    a.addr = 10 | (0 << 8) | (2 << 16) | ((DWORD)2 << 24);
    if (connect(s, &a, sizeof a) != 0) { closesocket(s); return 0; }
    send(s, rq, n, 0);
    for (;;) {
        n = recv(s, rp + got, (int)sizeof rp - 1 - got, 0);
        if (n <= 0) break;
        got += n;
        if (got >= (int)sizeof rp - 1) break;
    }
    closesocket(s);
    rp[got] = 0;
    for (i = 0; i + 3 < got; i++)
        if (rp[i] == '\r' && rp[i + 1] == '\n' && rp[i + 2] == '\r' && rp[i + 3] == '\n') {
            int k = 0; i += 4;
            while (i < got) rp[k++] = rp[i++];
            rp[k] = 0;
            while (k && (rp[k - 1] == '\r' || rp[k - 1] == '\n' || rp[k - 1] == ' ')) rp[--k] = 0;
            return 1;
        }
    return 0;
}

static void sendresult(DWORD id, const char *text) {
    static const char hex[] = "0123456789ABCDEF";
    int n = app(rq, 0, sizeof rq - 8, "/result?id=");
    n = appnum(rq, n, sizeof rq - 8, id);
    n = app(rq, n, sizeof rq - 8, "&text=");
    /* get() builds "GET <path> ..." in rq itself, so the path is staged in a second buffer */
    {
        static char path[MAXTEXT * 3 + 64];
        int m = 0, i;
        for (i = 0; i < n; i++) path[m++] = rq[i];
        for (i = 0; text[i] && m < (int)sizeof path - 4; i++) {
            unsigned char c = (unsigned char)text[i];
            if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) path[m++] = (char)c;
            else { path[m++] = '%'; path[m++] = hex[c >> 4]; path[m++] = hex[c & 15]; }
        }
        path[m] = 0;
        get(path);
    }
}

static void do_run(char *cmd) {
    SI si; PI pi;
    memset(&si, 0, sizeof si);
    si.cb = sizeof si;
    memset(&pi, 0, sizeof pi);
    if (CreateProcessA(0, cmd, 0, 0, 0, 0, 0, 0, &si, &pi)) {
        put("ok pid="); putnum(pi.pid);
        CloseHandle(pi.proc); CloseHandle(pi.thread);
    } else { put("err "); putnum(GetLastError()); }
}

static void do_ps(void) {
    HANDLE h = CreateToolhelp32Snapshot(2, 0);
    PE e;
    if (h == (HANDLE)-1) { put("err "); putnum(GetLastError()); return; }
    memset(&e, 0, sizeof e); e.size = sizeof e;
    if (Process32First(h, &e)) do {
        putnum(e.pid); put(" "); put(e.exe); put("\n");
    } while (Process32Next(h, &e));
    CloseHandle(h);
}

static void do_kill(const char *name) {
    HANDLE h = CreateToolhelp32Snapshot(2, 0);
    PE e; DWORD k = 0, me = GetCurrentProcessId();
    if (h == (HANDLE)-1) { put("err "); putnum(GetLastError()); return; }
    memset(&e, 0, sizeof e); e.size = sizeof e;
    if (Process32First(h, &e)) do {
        if (e.pid != me && ieq(e.exe, name)) {
            HANDLE p = OpenProcess(1, 0, e.pid);
            if (p) { if (TerminateProcess(p, 1)) k++; CloseHandle(p); }
        }
    } while (Process32Next(h, &e));
    CloseHandle(h);
    if (k) { put("killed "); putnum(k); } else put("none");
}

void start(void) {
    static char wsa[512];
    WSAStartup(0x0202, wsa);
    for (;;) {
        if (get("/cmd") && rp[0] >= '0' && rp[0] <= '9') {
            DWORD id = 0; char *c = rp;
            while (*c >= '0' && *c <= '9') id = id * 10 + (DWORD)(*c++ - '0');
            while (*c == ' ') c++;
            outn = 0; out[0] = 0;
            if (ieq(c, "ping")) put("pong");
            else if (ieq(c, "ps")) do_ps();
            else if (ieq(c, "exit")) { put("bye"); sendresult(id, out); ExitProcess(0); }
            else if (starts(c, "run ")) do_run(c + 4);
            else if (starts(c, "kill ")) do_kill(c + 5);
            else { put("err unknown command: "); put(c); }
            sendresult(id, out);
            continue;  /* more may be queued: ask again at once */
        }
        Sleep(1000);
    }
}
