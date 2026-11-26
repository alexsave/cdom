#include "cdom.h"
#include "parts.h"
#include "name.h"

static cd_region profile_region;
static cd_id input;

static void on_input(const cd_event_rec *, void *) {
    char buf[NAME_CAP];
    uint32_t n;
    if (!cd_input_whole(input, buf, sizeof buf, &n)) n = cd_get_str(input, "value", buf, sizeof buf);
    name_set(buf, n);
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
