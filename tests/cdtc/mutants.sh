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
m 's/\Qout(c, "      if ((cd_str)e->key == cdt_s[%d]) ", lit(a->name + 3));\E/out(c, "      if (1) ");/' 'FAIL golden widget: generated code differs'
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
m 's/\Qif (cdt_t != cdt_v->t%d) {\E/if (1 || cdt_t != cdt_v->t%d) {/' 'FAIL op counts: a label patch is one call'
m 's/\Q1 + walk_cost(0, 0,\E/0 * walk_cost(0, 0,/' 'FAIL golden nested' # a clone's walk climbs rather than restarting at the root
# template slots: the slot never marked (the sink refuses the slot), a slotted text also walked to (more calls)
m 's/\Qif (x->ts) out(b, " cd_slot(%d);", x->ts - 1);\E//' 'cdom_html: no such template slot'
m 's/\QN[n].ts || !create_action(n)\E/!create_action(n)/' 'FAIL op counts: a templated row creates in 5 calls'
exit $bad
