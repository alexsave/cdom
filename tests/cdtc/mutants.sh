#!/bin/bash
# Mutation checks for tests/cdtc/run.sh: each mutant of tools/cdtc.c must make the run fail
# with the named assertion (tools/mutate.sh restores the file from a copy). Usage: tests/cdtc/mutants.sh
set -u
cd "$(dirname "$0")/../.."
bad=0
m() { tools/mutate.sh tools/cdtc.c "$1" "$2" tests/cdtc/run.sh || bad=1; }
# goldens
m 's/\Qout(b, " cd_down();");\E/out(b, " cd_down(); cd_down();");/' 'FAIL golden adjacent: generated code differs'
m 's/\Qif (u32s) {\E/if (1) {/' 'FAIL golden text: generated C does not compile cleanly'
# error rules
m 's/\Qif (ptag && table_ctx(ptag)) fail(\E/if (0) fail(/' 'FAIL error table_text'
m 's/\Qif (!ok || in_list(tag, ok)) return;\E/return;/' 'FAIL error table_div'
m 's/\Qif (!strcmp(ostk[i].tag, "p")) fail(\E/if (!strcmp(ostk[i].tag, "q")) fail(/' 'FAIL error p_in_p'
m 's/\Qif (in_list(tag, VOIDS) || N[me].self_close) return;\E/if (N[me].self_close) return;/' 'FAIL error void_children'
m 's/\Qif (t == END_TAG && !strcmp(term_name, tag)) {\E/if (t == END_TAG) {/' 'FAIL error unclosed: exit'
m 's/\Qif (top.n != 1 ||\E/if (top.n < 1 ||/' 'FAIL error two_roots'
m 's/\Q|| !r->self_close \E//' 'FAIL error each_body'
m 's/\Qif (!okname)\E/if (0)/' 'FAIL error bad_event'
m 's/\Qif (N[me].host && ck.n)\E/if (0)/' 'FAIL error host_children'
m 's/\Qif (x->host && !x->ref) fail(\E/if (0) fail(/' 'FAIL error host_noref'
m 's/\Q{"id", "c",\E/{"xx", "c",/' 'FAIL error ref_reserved'
m 's/\Qif (N[m].kind == N_ELEM && N[m].ref && !strcmp(N[m].ref, x->ref))\E/if (0)/' 'FAIL error ref_twice'
m 's/\Qcdt_v->%s = cdt_root;\E/cdt_v->%s = 0;/' 'FAIL golden widget: generated code differs'
m 's/\Qout(c, "      if ((cd_str)e->key == cdt_s[%d]) { \/* %s *\/", lit(a->name + 3), cmt(a->name + 3));\E/out(c, "      if (1) {");/' 'FAIL golden widget: generated code differs'
m 's/\Qelse fail(x->at, "cannot infer\E/else if (0) fail(x->at, "cannot infer/' 'FAIL error untyped'
# the round trip
m 's/\Qfor (int i = 0; i < fl - c - 1; i++) out(b, " cd_up();");\E/for (int i = 0; i < fl - c; i++) out(b, " cd_up();");/' 'cdom_html: no node with that id' # nested: the sink refuses the close
m 's/\Qif (m >> t++ & 1)\E/if (0 \&\& (m >> t++ \& 1))/' 'FAIL first render'
m 's/\Qstrcat(b, idx(c));\E/strcat(b, "0");/' 'FAIL flag and mode 1'
m 's/\Qif (cdt_t != cdt_v->t%d) {\E/if (0 \&\& cdt_t != cdt_v->t%d) {/' 'FAIL title and n change'
m 's/\Qcdt_v->key = %s_keys%d[cdt_i];\E/cdt_v->key = 0 * %s_keys%d[cdt_i];/g' 'FAIL item clicks' # page and nested lists
m 's/\Q|| o != len) return -1;\E/) return -1;/' 'FAIL a blob with a trailing byte is refused'
# nested lists (tests/cdtc/nested_find.c)
m 's/\Q%s *cdt_p = %s_find%d(%s, cdt_v->pvi);\E/%s *cdt_p = %s_find%d(%s, 0);/' 'FAIL first render'
m 's/\Qcd_id_free(cdt_v->list%d.items[i].node);\E/(void)cdt_v->list%d.items[i].node;/' 'FAIL a column removed'
m 's/\Qif (f == F[f].tpl && !t->page && t_lists_of(f)) out(\E/if (0) out(/' 'FAIL every list reversed: the page shows'
# op counts against the hand-written app
# a text binding that always writes: the marked render only reaches marked bindings, so the full render's
# compare is what notices (each test asserts a full render after it changes nothing)
m 's/\Qif (cdt_t != cdt_v->t%d) {\E/if (1 || cdt_t != cdt_v->t%d) {/' 'calls the marked render missed'
m 's/\Q1 + walk_cost(0, 0,\E/0 * walk_cost(0, 0,/' 'FAIL golden nested' # a clone's walk climbs rather than restarting at the root
# template slots: the slot never marked (the sink refuses the slot), a slotted text also walked to (more calls)
m 's/\Qif (x->ts) out(b, " cd_slot(%d);", x->ts - 1);\E//' 'FAIL round trip' # cd_tpl_text on a slot never marked traps
m 's/\QN[n].ts || !create_action(n)\E/!create_action(n)/' 'FAIL op counts: a templated row creates in 5 calls'
# signals: what a setter marks, and what the render does with the marks
m 's/\Q", p, w, f->rd.w[w]);\E/", p, w, 0u);/' 'FAIL title and n change' # setters mark nothing
m 's/\Q"  if (cdt_qn_%s < CDT_QCAP) cdt_q_%s\E/"  if (0 * cdt_qn_%s) cdt_q_%s/' 'FAIL one label: one call, one compare, one row visited' # nothing queued: every render scans every item
m 's/\Qif (a >= 0 && rec_shown[a] && f->kd) out(c, "  cdt_kd_%s = 1;\E..\Q", recs[a].name);\E//' 'FAIL a key change' # a key's setter does not flag the list
m 's/\Q(cdt_m[%d] & 0x%xu ? 0x%xu : 0u)", terms++\E/(0u * cdt_m[%d] * 0x%xu * 0x%xu)", terms++/' 'FAIL a key change (remove' # no list-wide words: the select after it compares nothing
m 's/\Qif (x->ctx < 0) out(b, "if (cdt_m[%d] & 0x%xu) ",\E/if (x->ctx < 0) out(b, "if (1 || cdt_m[%d] \& 0x%xu) ",/' 'FAIL one label: one call, one compare' # every binding of a marked item compared
m 's/\Qif (d->rd.w[w]) out(c, "      S->cdt_d[%d] |= 0x%xu;\E..\Q", w, d->rd.w[w]);\E/(void)w;/' 'FAIL an input of a derived chain' # a changed derived marks no reader
m 's/\Qout(b, "  %s->cdt_d[%d] = 0;\E..\Q", t->ivar, w);\E/out(b, "  (void)%s; (void)%d;\\n", t->ivar, w);/' 'FAIL a key change' # a created item keeps marks from before
m 's/\Qout(&rd, "  cdt_epoch++;\E/out(\&rd, "  cdt_epoch += 0;/' 'FAIL one row in two renders' # a record queued once is never queued again
m 's/\Qout(&rd, "  } else if (cdt_qn_%s > CDT_QCAP", R);\E/out(\&rd, "  } else if (0 * cdt_qn_%s", R);/' 'FAIL 200 labels' # an overflowed queue is not scanned
m 's/\Qout(c, "  cdt_m[%d] = %s->cdt_d[%d] | cdt_lw_%s[%d];\E..\Q  %s->cdt_d[%d] = 0;\E..\Q", w, rv, w, T[r].name, w, rv, w);\E/out(c, "  cdt_m[%d] = %s->cdt_d[%d] | cdt_lw_%s[%d];\\n", w, rv, w, T[r].name, w);/' 'FAIL swap' # an item's marks never cleared
m 's/\Qout(&rd, "  if (cdt_qn_%s || cdt_kd_%s", recs[cr2].name, recs[cr2].name);\E/out(\&rd, "  if (0 * cdt_qn_%s * cdt_kd_%s", recs[cr2].name, recs[cr2].name);/' 'FAIL a tag renamed' # a nested item's mark does not climb
m 's/\Q  cdt_full = 1; \/* everything changed *\/\E..\Q\E//' 'FAIL a state loaded into the live page' # a load marks nothing
# the dev build's reports
m 's/\Qout(c, "  if (!all && S->%s != S->cdt_sh_%s) cd_dev_warn(\E/out(c, "  if (0 \&\& S->%s != S->cdt_sh_%s) cd_dev_warn(/' 'FAIL a direct write to Rows.selected'
m 's/\Qout(c, "  if (!all && r->%s != r->cdt_sh_%s) cd_dev_warn(\E/out(c, "  if (0 \&\& r->%s != r->cdt_sh_%s) cd_dev_warn(/' "FAIL a direct write to a row's label"
m 's/\Qout(c, "    if (bad) cd_dev_warn(\E/out(c, "    if (0 \&\& bad) cd_dev_warn(/' 'FAIL a reorder without a mark'
exit $bad
