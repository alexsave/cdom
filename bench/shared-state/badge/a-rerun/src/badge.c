#include "cdom_ui.h"
#include "parts.h"
#include "state.h"
#include "views.h"

static cd_region badge_region;
static cd_bound hi;

/* Runs on every change to any state, so it formats every time; cd_bound skips the DOM write when the
 * text came out the same. */
static void badge_view(void) {
    char room[4 + NAME_CAP];
    cd_buf b = cd_buf_in(room, sizeof room);
    cd_buf_putz(&b, "Hi, ");
    cd_buf_put(&b, name, name_len);
    cd_bound_set_bytes(&hi, b.p, b.len);
}
static void unbind(void *) { cd_unbind(&hi); }

void badge_show(void) {
    if (!cd_region_show(&badge_region, 1)) return;
    cd_id t = cd_id_new();
    cd_open("span"); cd_name_open(badge_region.root);
    cd_text(""); cd_name_text(t);
    cd_close();
    cd_bind_text(&hi, t);
    cd_on_free(nullptr, unbind, nullptr);
    view_add(badge_view);
    badge_view();
    cd_region_end(&badge_region);
}
