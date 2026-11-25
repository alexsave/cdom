/* The image module's example: a file input; the picked photo's bytes come into C (cd_file_read_cb), C resizes them to
 * fit 900 x 900 with the EXIF orientation applied (cd_image_resize), the browser encodes the pixels as a JPEG
 * (cd_encode_image), and the JPEG is shown (cd_set_image_bytes). Every status is said in words here: cdom has none. */
#include "cdom.h"
#include "cdom_file.h"
#include "cdom_image.h"

enum { ENCODED = 1, SHOWN = 2 };
#define BOX 900
#define FILE_MAX 40000000u

static cd_id input, note, picture;
static cd_req pick;   /* only the latest pick's answer is shown */

static const char *say(uint32_t status) {
    switch (status) {
        case CD_IMAGE_NOT_IMAGE: return "That file is not a JPEG or a PNG.";
        case CD_IMAGE_TOO_BIG: return "That photo is too big to resize here.";
        case CD_IMAGE_UNSUPPORTED: return "That kind of JPEG or PNG is not supported.";
        case CD_IMAGE_CORRUPT: return "That photo is damaged or cut short.";
        case CD_IMAGE_NO_ROOM: return "There is not enough memory to resize that photo.";
        default: return "That photo could not be resized.";
    }
}

static void got(void *ctx, uint32_t status, const uint8_t *p, uint32_t len) {
    if (!cd_req_current(&pick, ctx, 0)) return;   /* another photo was picked since */
    if (status == CD_FILE_TOO_BIG) { cd_set_text_node(note, "That file is too big."); return; }
    if (status != CD_FILE_OK) { cd_set_text_node(note, "That file could not be read."); return; }
    uint8_t *rgba;
    uint32_t w, h;
    uint32_t st = cd_image_resize(p, len, BOX, BOX, &rgba, &w, &h);
    if (st != CD_IMAGE_OK) { cd_set_text_node(note, say(st)); return; }
    cd_encode_image(rgba, w, h, 4, "image/jpeg", 85, ENCODED);
    cd_free(rgba);
    cd_set_text_node(note, "Encoding...");
}

void cd_on_encoded(uint32_t token, const uint8_t *p, uint32_t len) {
    if (token == ENCODED) cd_set_image_bytes(picture, "background-image", p, len, "image/jpeg", SHOWN);
}
void cd_on_image(uint32_t token, uint32_t ok) {
    if (token == SHOWN) cd_set_text_node(note, ok ? "Resized in C." : "The resized photo did not decode.");
}

static void on_change(const cd_event_rec *, void *) {
    cd_set_text_node(note, "Reading...");
    cd_file_read_cb(input, 0, FILE_MAX, got, cd_req_begin(&pick, 0));
}

void cd_main(void) {
    cd_open("h1"); cd_text("Photo"); cd_close();
    input = cd_open_id("input");
    cd_attr("type", "file");
    cd_attr("accept", "image/jpeg,image/png");
    cd_close();
    cd_on(input, CD_EV_CHANGE, on_change, 0);
    cd_open("p"); cd_text("Pick a JPEG or PNG."); cd_name_text(note = cd_id_new()); cd_close();
    picture = cd_open_id("div");
    cd_attr("style", "width: 900px; height: 900px; max-width: 100%; background: center / contain no-repeat");
    cd_close();
}
