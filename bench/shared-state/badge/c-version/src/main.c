/* The same tree in all three: header > nav > badge, and section > settings > profile. The badge and the
 * textbox are in different regions, in different files, and neither file includes the other. */
#include "cdom.h"
#include "parts.h"

static cd_region header, settings;

void cd_main(void) {
    cd_open("main");
    cd_id head = cd_open_id("header"); cd_close();
    cd_id body = cd_open_id("section"); cd_close();
    cd_close();

    header.parent = head;
    if (cd_region_show(&header, 1)) {
        cd_at(head, CD_APPEND);
        cd_open("nav"); badge_show(); cd_close();
        cd_region_end(&header);
    }
    settings.parent = body;
    if (cd_region_show(&settings, 1)) {
        cd_at(body, CD_APPEND);
        cd_open("form"); profile_show(); cd_close();
        cd_region_end(&settings);
    }
}
