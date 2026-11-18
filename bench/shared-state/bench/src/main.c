/* The DOM, the same in every entry:
 *   <div><table><tbody> the rows </tbody></table></div>   the table region (all the div's children)
 *   <div> 10 x <div> 10 x <div> 10 x <span>f7=0</span>     1,000 leaf regions, three levels deep
 *   <span>0</span>                                          the draft's length
 *   <textarea></textarea>                                   the big text's input
 *   <div>...</div>                                          the big text's mirror
 * Each entry's views are made in the builds through the hooks of app.h. */
#include "app.h"

cd_list list;
cd_region table_region;
cd_id textarea;
static cd_id tablebox, smalls;
static cd_region l1[10], l2[100], l3[LEAVES], draft_region, ta_region, mirror_region;

cd_id row_build(uint32_t i, cd_id *txt, int with_text) {
  const row_t *r = &rows[i];
  cd_id tr = cd_open_id("tr");
  cd_class(sel_cls[r->id == selected]);
  cd_open("td"); cd_text_num(r->id); cd_close();
  cd_open("td");
  if (with_text) {
    char room[32];
    cd_text(cd_tmp(room, price_text(room, sizeof room, r)));
  } else {
    cd_text("");
  }
  cd_name_text(*txt = cd_id_new());
  cd_close();
  cd_close();
  return tr;
}
void row_write_text(cd_id txt, uint32_t i) {
  char room[32];
  cd_set_text_node(txt, cd_tmp(room, price_text(room, sizeof room, &rows[i])));
}

static void forget_rows(void *) { cd_list_forget(&list, &row_fns); }
void table_show(void) {
  if (!cd_region_show(&table_region, 1)) return;
  cd_at(tablebox, CD_APPEND);
  cd_open("table");
  list.parent = cd_open_id("tbody");
  list.owns_parent = 1;
  cd_close();
  cd_close();
  cd_on_free(nullptr, forget_rows, nullptr);
  table_built();
  cd_region_end(&table_region);
}

static void smalls_show(void) {
  for (uint32_t a = 0; a < 10; a++) {
    cd_at(smalls, CD_APPEND);
    if (!cd_region_show(&l1[a], 1)) continue;
    cd_open("div"); cd_name_open(l1[a].root);
    for (uint32_t b = 0; b < 10; b++) {
      cd_region *m = &l2[a * 10 + b];
      if (!cd_region_show(m, 1)) continue;
      cd_open("div"); cd_name_open(m->root);
      for (uint32_t c = 0; c < 10; c++) {
        uint32_t leaf = a * 100 + b * 10 + c;
        if (!cd_region_show(&l3[leaf], 1)) continue;
        cd_open("span"); cd_name_open(l3[leaf].root);
        cd_id txt;
        cd_text(""); cd_name_text(txt = cd_id_new());
        cd_close();
        leaf_built(leaf, leaf % NF, txt);
        cd_region_end(&l3[leaf]);
      }
      cd_close();
      cd_region_end(m);
    }
    cd_close();
    cd_region_end(&l1[a]);
  }
}

void cd_main(void) {
  cd_at(CD_MOUNT, CD_APPEND);
  tablebox = cd_open_id("div"); cd_close();
  smalls = cd_open_id("div"); cd_close();
  table_region.parent = tablebox; /* the region is all of tablebox's children */
  table_show();
  smalls_show();
  cd_id txt;
  cd_at(CD_MOUNT, CD_APPEND);
  if (cd_region_show(&draft_region, 1)) {
    cd_open("span"); cd_name_open(draft_region.root);
    cd_text(""); cd_name_text(txt = cd_id_new());
    cd_close();
    draft_built(txt);
    cd_region_end(&draft_region);
  }
  cd_at(CD_MOUNT, CD_APPEND);
  if (cd_region_show(&ta_region, 1)) {
    cd_open("textarea"); cd_name_open(ta_region.root); cd_close();
    textarea = ta_region.root;
    cd_region_end(&ta_region);
  }
  cd_at(CD_MOUNT, CD_APPEND);
  if (cd_region_show(&mirror_region, 1)) {
    cd_open("div"); cd_name_open(mirror_region.root);
    cd_text(""); cd_name_text(txt = cd_id_new());
    cd_close();
    mirror_built(txt);
    cd_region_end(&mirror_region);
  }
}
