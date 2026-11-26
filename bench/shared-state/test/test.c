/* The cd_state prototype's checks (bench/shared-state/lib), one group of exports per check; test.mjs calls
 * them in headless Chrome. Every export ends in cd_flush(), as an entry from JS does, and returns what the
 * check reads after it. Built twice by build.sh: as it is, and with -DCD_STATE_CHECK (check 8). */
#include "cdom_ui.h"
#include "cd_state.h"

#define EXPORT(n) __attribute__((export_name(n)))

/* A view's text node and how often it ran; each view writes its count, so a view that runs after its
 * region went writes to a freed id, and the shim throws. */
typedef struct { cd_id txt; uint32_t calls; } view_t;
static void view_write(view_t *v) {
    v->calls++;
    char room[16];
    cd_buf b = cd_buf_in(room, sizeof room);
    cd_buf_u64(&b, v->calls);
    cd_set_text_node(v->txt, cd_tmp(b.p, b.len));
}
/* A region holding one <span> with one text node, for view v. */
static void span_open(cd_region *r, view_t *v) {
    cd_open("span"); cd_name_open(r->root);
    cd_text(""); cd_name_text(v->txt = cd_id_new());
    cd_close();
}
static int show(cd_region *r) { cd_at(CD_MOUNT, CD_APPEND); return cd_region_show(r, 1); }

EXPORT("watchers") uint32_t watchers_of(uint32_t which);
EXPORT("runs") uint32_t runs(void) { return cd_state_runs(); }
void cd_main(void) {}

/* 1. One view watches s1a and s1b; s1a changes 1,000 times and s1b once in one entry: it runs once. */
static cd_state s1a, s1b;
static cd_region r1;
static view_t v1;
static void view1(void *) { view_write(&v1); }
EXPORT("t1_setup") uint32_t t1_setup(void) {
    if (show(&r1)) {
        span_open(&r1, &v1);
        cd_watch(&s1a, view1, nullptr);
        cd_watch(&s1b, view1, nullptr);
        view1(nullptr);
        cd_region_end(&r1);
    }
    cd_flush();
    return v1.calls;
}
EXPORT("t1_go") uint32_t t1_go(void) {
    uint32_t before = v1.calls;
    for (int i = 0; i < 1000; i++) cd_changed(&s1a);
    cd_changed(&s1b);
    cd_flush();
    return v1.calls - before;
}

/* 2. s2 changes, then the region of the view watching it goes, in the same entry: the view does not run. */
static cd_state s2;
static cd_region r2;
static view_t v2;
static void view2(void *) { view_write(&v2); }
EXPORT("t2_setup") uint32_t t2_setup(void) {
    if (show(&r2)) {
        span_open(&r2, &v2);
        cd_watch(&s2, view2, nullptr);
        view2(nullptr);
        cd_region_end(&r2);
    }
    cd_flush();
    return v2.calls;
}
EXPORT("t2_go") uint32_t t2_go(void) {
    uint32_t before = v2.calls;
    cd_changed(&s2);
    cd_region_hide(&r2);
    cd_flush();
    return v2.calls - before;
}

/* 3. Two views of s3 queued in one round; the first hides the region of the second, which does not run. */
static cd_state s3;
static cd_region r3a, r3b;
static view_t v3a, v3b;
static void view3a(void *) { view_write(&v3a); if (v3a.calls > 1) cd_region_hide(&r3b); }
static void view3b(void *) { view_write(&v3b); }
EXPORT("t3_setup") uint32_t t3_setup(void) {
    if (show(&r3a)) { span_open(&r3a, &v3a); cd_watch(&s3, view3a, nullptr); view3a(nullptr); cd_region_end(&r3a); }
    if (show(&r3b)) { span_open(&r3b, &v3b); cd_watch(&s3, view3b, nullptr); view3b(nullptr); cd_region_end(&r3b); }
    cd_flush();
    return v3a.calls * 10 + v3b.calls;
}
EXPORT("t3_go") uint32_t t3_go(void) {
    uint32_t a = v3a.calls, b = v3b.calls;
    cd_changed(&s3);
    cd_flush();
    return (v3a.calls - a) * 10 + (v3b.calls - b); /* 10: the first ran, the second did not */
}

/* 4. A view of s4 shows a region whose build watches s4 too: the new view draws in its build and is not
 * run again in that round; at the next change both run. */
static cd_state s4;
static cd_region r4a, r4b;
static view_t v4a, v4b;
static void view4b(void *) { view_write(&v4b); }
static void view4a(void *) {
    view_write(&v4a);
    if (v4a.calls > 1 && show(&r4b)) {
        span_open(&r4b, &v4b);
        cd_watch(&s4, view4b, nullptr);
        view4b(nullptr);
        cd_region_end(&r4b);
    }
}
EXPORT("t4_setup") uint32_t t4_setup(void) {
    if (show(&r4a)) { span_open(&r4a, &v4a); cd_watch(&s4, view4a, nullptr); view4a(nullptr); cd_region_end(&r4a); }
    cd_flush();
    return v4a.calls;
}
/* returns runs of the pass * 100 + calls of view4a * 10 + calls of view4b */
EXPORT("t4_go") uint32_t t4_go(void) {
    uint32_t r = cd_state_runs(), a = v4a.calls, b = v4b.calls;
    cd_changed(&s4);
    cd_flush();
    return (cd_state_runs() - r) * 100 + (v4a.calls - a) * 10 + (v4b.calls - b);
}

/* 5. A view of s5a changes s5b: s5b's view runs in the same flush, as a second round. A view of s5c that
 * changes s5c every time it runs traps after 16 rounds. */
static cd_state s5a, s5b, s5c;
static cd_region r5a, r5b, r5c;
static view_t v5a, v5b, v5c;
static void view5a(void *) { view_write(&v5a); if (v5a.calls > 1) cd_changed(&s5b); }
static void view5b(void *) { view_write(&v5b); }
static void view5c(void *) { view_write(&v5c); if (v5c.calls > 1) cd_changed(&s5c); }
EXPORT("t5_setup") uint32_t t5_setup(void) {
    if (show(&r5a)) { span_open(&r5a, &v5a); cd_watch(&s5a, view5a, nullptr); view5a(nullptr); cd_region_end(&r5a); }
    if (show(&r5b)) { span_open(&r5b, &v5b); cd_watch(&s5b, view5b, nullptr); view5b(nullptr); cd_region_end(&r5b); }
    if (show(&r5c)) { span_open(&r5c, &v5c); cd_watch(&s5c, view5c, nullptr); view5c(nullptr); cd_region_end(&r5c); }
    cd_flush();
    return v5a.calls + v5b.calls + v5c.calls;
}
EXPORT("t5_go") uint32_t t5_go(void) { /* 11: both ran once in this flush */
    uint32_t a = v5a.calls, b = v5b.calls;
    cd_changed(&s5a);
    cd_flush();
    return (v5a.calls - a) * 10 + (v5b.calls - b);
}
EXPORT("t5_loop") uint32_t t5_loop(void) { /* traps; returns only when it does not */
    cd_changed(&s5c);
    cd_flush();
    return v5c.calls;
}

/* 6. A region hidden and shown again draws the value now at its build and follows later changes; after
 * the hide nothing watches s6. */
static cd_state s6;
static uint32_t value6;
static cd_region r6;
static view_t v6;
static void view6(void *) { cd_read(&s6); v6.calls++; cd_set_text_node_num(v6.txt, value6); }
static void set6(uint32_t v) { value6 = v; cd_changed(&s6); }
EXPORT("t6_show") uint32_t t6_show(void) {
    if (show(&r6)) { span_open(&r6, &v6); cd_watch(&s6, view6, nullptr); view6(nullptr); cd_region_end(&r6); }
    cd_flush();
    return watchers_of(6);
}
EXPORT("t6_hide") uint32_t t6_hide(void) { cd_region_hide(&r6); cd_flush(); return watchers_of(6); }
EXPORT("t6_set") uint32_t t6_set(uint32_t v) { set6(v); cd_flush(); return v6.calls; }

/* 7. Watchers in the caller's memory (cd_watch_at): view7a unwatches view7b, queued after it in the same
 * round, which then does not run; a watcher unwatched before the flush does not run either. */
static cd_state s7;
static cd_watcher w7a, w7b, w7c;
static uint32_t c7a, c7b, c7c;
static void view7a(void *) { c7a++; cd_unwatch(&w7b); }
static void view7b(void *) { c7b++; }
static void view7c(void *) { c7c++; }
EXPORT("t7_go") uint32_t t7_go(void) { /* 100: only view7a ran */
    cd_watch_at(&w7a, &s7, view7a, nullptr, nullptr);
    cd_watch_at(&w7b, &s7, view7b, nullptr, nullptr);
    cd_watch_at(&w7c, &s7, view7c, nullptr, nullptr);
    cd_changed(&s7);
    cd_unwatch(&w7c); /* not queued yet: the pass never sees it */
    cd_flush();
    uint32_t r = c7a * 100 + c7b * 10 + c7c;
    cd_unwatch(&w7a);
    return r;
}
/* what still watches s7 after t7_go: nothing */
EXPORT("t7_after") uint32_t t7_after(void) { return watchers_of(7); }

/* 8. The check build: a view that reads (cd_read) a state it does not watch traps; one that reads only what
 * it watches does not. s8b has a watcher of its own (view8other), so the read's search has a list to walk. */
static cd_state s8a, s8b;
static cd_region r8;
static view_t v8;
static uint32_t c8other, bad8;
static void view8(void *) { cd_read(&s8a); if (bad8) cd_read(&s8b); view_write(&v8); }
static void view8other(void *) { cd_read(&s8b); c8other++; }
EXPORT("t8_setup") uint32_t t8_setup(void) {
    if (show(&r8)) {
        span_open(&r8, &v8);
        cd_watch(&s8a, view8, nullptr);
        cd_watch(&s8b, view8other, nullptr);
        view8(nullptr); /* outside the flush: nothing runs, so cd_read does not check */
        cd_region_end(&r8);
    }
    cd_flush();
    return v8.calls;
}
EXPORT("t8_good") uint32_t t8_good(void) { cd_changed(&s8a); cd_changed(&s8b); cd_flush(); return v8.calls * 10 + c8other; }
EXPORT("t8_bad") uint32_t t8_bad(void) { bad8 = 1; cd_changed(&s8a); cd_flush(); return v8.calls; }
EXPORT("check_build") uint32_t check_build(void) {
#ifdef CD_STATE_CHECK
    return 1;
#else
    return 0;
#endif
}

static cd_state *const all[] = {[1] = &s1a, [2] = &s2, [3] = &s3, [4] = &s4, [5] = &s5a, [6] = &s6, [7] = &s7, [8] = &s8a};
uint32_t watchers_of(uint32_t which) {
    uint32_t n = 0;
    for (cd_watcher *w = all[which]->first; w; w = w->next) n++;
    return n;
}
