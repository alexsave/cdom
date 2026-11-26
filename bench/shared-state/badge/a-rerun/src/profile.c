#include "cdom.h"
#include "parts.h"
#include "state.h"
#include "views.h"

static cd_region profile_region;
static cd_id input;

static void on_input(const cd_event_rec *, void *) {
    if (!cd_input_whole(input, name, sizeof name, &name_len))
        name_len = cd_get_str(input, "value", name, sizeof name);
    views_changed();
}

void profile_show(void) {
    if (!cd_region_show(&profile_region, 1)) return;
    cd_open("label"); cd_name_open(profile_region.root);
    cd_text("Name ");
    input = cd_open_id("input"); cd_close();
    cd_close();
    cd_on(input, CD_EV_INPUT, on_input, nullptr);
    cd_region_end(&profile_region);
}
