/* The Ultimate Tic-Tac-Toe replay viewer: the cdom twin of uttt.live, and the first app made for the frames tier
 * (docs/frames-design.md: Netscape 2 to 4, IE 3 and 4). It opens a game code (or a whole https://uttt.live/<code>
 * link) from a text field or from the page address's query (uttt.html?<code>), and steps through the game: |< to
 * the start, < back a move, Play or Pause (a move every 600 ms), > forward a move, >| to the end.
 *
 * The rules and the code are foolish's (uttt/c/src/uttt.c and uttt_code.c, compiled beside this file from a checkout
 * at FOOLISH: demo/uttt/Makefile); this file owns the page and nothing of the game.
 *
 * Made for Netscape 2's limits: tables, TT, B and colours only (the CSS here is what the frames tier writes as FONT
 * COLOR, FONT SIZE and a TABLE's BGCOLOR), and click and change events only. The board is built again whole at each
 * step (at most 81 cells and 9 blocks), which on the frames tier is one rewrite of the frame anyway. */
#include "../../src/cdom.h"
#include "../../src/cdom_url.h"
#include "uttt_code.h"
#include <string.h>

#define TICK_MS 600

static UtttGame game;    /* the decoded game: all its plies */
static int have;         /* a game is open */
static int shown;        /* the ply shown: 0 to game.n_plies */
static int playing;
static cd_timer_slot timer;
static cd_id field, board, err_txt, play_txt, status_txt;
static cd_id b_open, b_start, b_back, b_play, b_fwd, b_end;
static char buf[260];

/* The position after the first k plies of the game: replayed from the start, at most 81 moves. */
static void position(UtttGame *g, int k) {
    uttt_init(g);
    for (int p = 0; p < k; p++) uttt_play(g, game.move[p]);
}

static uint32_t fmt_id(char *out, char c, unsigned n) {
    uint32_t i = 0;
    out[i++] = c;
    if (n >= 10) out[i++] = (char)('0' + n / 10);
    out[i++] = (char)('0' + n % 10);
    return i;
}

static const char *const MARK[] = {".", "X", "O"};
static const char *const MARK_STYLE[] = {"color:#999999", "color:#cc0000", "color:#0000cc"};
static const char *const BIG_STYLE[] = {"", "color:#cc0000;font-size:32px;font-weight:bold", "color:#0000cc;font-size:32px;font-weight:bold"};

/* One block: a won block is its one big mark; an open or drawn one is its 3 x 3 cells. Either is 66 x 66 pixels, so
 * the board never changes size. The block's background, when it has one, comes from the DIV around it: the
 * forced block (yellow), a block of the winning line (green), a drawn block (grey). */
static void draw_block(const UtttGame *g, int b, int last) {
    char id[4];
    uint8_t won = uttt_block(g, b);
    unsigned legal = g->over ? 0 : uttt_legal_blocks(g);
    int line = uttt_won_line(g);
    const char *bg = nullptr;
    if ((legal >> b) & 1u) bg = "background-color:#ffff66";
    else if (line >= 0 && ((uttt_line_mask(line) >> b) & 1u)) bg = "background-color:#99ee99";
    else if (won == UTTT_DRAW) bg = "background-color:#cccccc";
    cd_open("td"); cd_attr("id", cd_tmp(id, fmt_id(id, 'b', (unsigned)b))); cd_attr("align", "center"); cd_attr("valign", "middle");
    if (bg) { cd_open("div"); cd_attr("style", bg); }
    cd_open("table"); cd_attr("width", "66"); cd_attr("height", "66"); cd_attr("border", "0");
    cd_attr("cellspacing", "0"); cd_attr("cellpadding", "0");
    if (won == UTTT_X || won == UTTT_O) {
        cd_open("tr"); cd_open("td"); cd_attr("align", "center"); cd_attr("valign", "middle");
        cd_open("span"); cd_attr("id", cd_tmp(id, fmt_id(id, 'm', (unsigned)b))); cd_attr("style", BIG_STYLE[won]);
        cd_text(MARK[won]);
        cd_close(); cd_close(); cd_close();
    } else {
        for (int r = 0; r < 3; r++) {
            cd_open("tr");
            for (int c = 0; c < 3; c++) {
                int i = b * 9 + r * 3 + c;
                uint8_t m = uttt_cell(g, i);
                cd_open("td"); cd_attr("id", cd_tmp(id, fmt_id(id, 'c', (unsigned)i)));
                cd_attr("width", "22"); cd_attr("height", "22"); cd_attr("align", "center");
                cd_open("span"); cd_attr("style", won == UTTT_DRAW && m ? "color:#666666" : MARK_STYLE[m]);
                if (i == last) cd_open("b");
                cd_text(MARK[m]);
                if (i == last) cd_close();
                cd_close();
                cd_close();
            }
            cd_close();
        }
    }
    cd_close();
    if (bg) cd_close();
    cd_close();
}

static void show(void) {
    UtttGame g;
    position(&g, shown);
    cd_clear(board);
    cd_at(board, CD_APPEND);
    cd_open("tt");
    cd_open("table"); cd_attr("border", "1"); cd_attr("cellspacing", "0"); cd_attr("cellpadding", "4");
    for (int r = 0; r < 3; r++) {
        cd_open("tr");
        for (int c = 0; c < 3; c++) draw_block(&g, r * 3 + c, shown ? game.move[shown - 1] : -1);
        cd_close();
    }
    cd_close();
    cd_close();
    cd_at(CD_MOUNT, CD_APPEND);
    /* "Move k of n. X to play.", and at the end the result. */
    uint32_t n = 0;
    const char *s = "Move ";
    while (*s) buf[n++] = *s++;
    n += cd_fmt(buf + n, shown, 0, CD_UNIT_NONE);
    s = " of ";
    while (*s) buf[n++] = *s++;
    n += cd_fmt(buf + n, game.n_plies, 0, CD_UNIT_NONE);
    s = g.over == UTTT_X ? ". X wins." : g.over == UTTT_O ? ". O wins." : g.over == UTTT_DRAW ? ". Draw."
      : shown == game.n_plies ? (g.turn == UTTT_X ? ". X to play; the game stops here." : ". O to play; the game stops here.")
      : g.turn == UTTT_X ? ". X to play." : ". O to play.";
    while (*s) buf[n++] = *s++;
    cd_set_text_node(status_txt, cd_tmp(buf, n));
}

static void set_playing(int on) {
    playing = on;
    if (!on) cd_timer_clear(&timer);
    cd_set_text_node(play_txt, on ? "Pause" : "Play");
}

static void tick(void *) {
    if (!playing || !have) return;
    if (shown < game.n_plies) shown++;
    show();
    if (shown >= game.n_plies) set_playing(0);
    else cd_timer_set(&timer, TICK_MS, tick, nullptr);
}

/* A code, or a link with it: leading and trailing blanks, and anything up to "uttt.live/", are not the code. */
static void open_code(char *s, uint32_t len) {
    while (len && (s[len - 1] == ' ' || s[len - 1] == '\t' || s[len - 1] == '\n' || s[len - 1] == '\r')) len--;
    s[len] = 0;
    while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') s++;
    for (char *p = s; *p; p++)
        if (!strncmp(p, "uttt.live/", 10)) { s = p + 10; break; }
    set_playing(0);
    UtttGame g;
    if (*s && uttt_replay_read(s, &g, nullptr) && g.n_plies) {
        game = g;
        have = 1;
        shown = 0;
        cd_set_text_node(err_txt, "");
        show();
    } else {
        have = 0;
        cd_clear(board);
        cd_set_text_node(err_txt, "That is not a game code. A code looks like AJG7NYNQG4RFXXGVPGD7QR2ETSOYTWMCREAQ.");
        cd_set_text_node(status_txt, "No game open.");
    }
}

static void on_open(const cd_event_rec *, void *) {
    uint32_t n = cd_get_str(field, "value", buf, sizeof buf - 1);
    if (n >= sizeof buf - 1) n = 0; /* longer than any code: not one */
    open_code(buf, n);
}

static void on_step(const cd_event_rec *e, void *) {
    if (!have) return;
    set_playing(0);
    cd_id id = (cd_id)e->id;
    if (id == b_start) shown = 0;
    else if (id == b_back && shown > 0) shown--;
    else if (id == b_fwd && shown < game.n_plies) shown++;
    else if (id == b_end) shown = game.n_plies;
    show();
}

static void on_play(const cd_event_rec *, void *) {
    if (!have) return;
    if (playing) { set_playing(0); return; }
    if (shown >= game.n_plies) { shown = 0; show(); }
    set_playing(1);
    cd_timer_set(&timer, TICK_MS, tick, nullptr);
}

static cd_id button(const char *idattr, const char *label) {
    cd_text(" ");
    cd_id id = cd_open_id("button");
    cd_attr("id", idattr);
    cd_text(label);
    cd_close();
    return id;
}

void cd_main(void) {
    cd_open("h4"); cd_text("Ultimate Tic-Tac-Toe replay"); cd_close();
    /* A small heading (the page without one hung Netscape 2.02 twice in two runs, and with it never in six: unproven, docs/frames-design.md 17) and the
     * buttons above the board: the page then fits the 640 x 480 screen with the toolbar, the location and the directory
     * buttons off, board and move count in view together. */
    cd_open("p");
    cd_text("Code or uttt.live link: ");
    field = cd_open_id("input"); cd_attr("id", "code"); cd_attr("size", "44"); cd_close();
    b_open = button("open", "Open");
    cd_close();
    cd_open("p"); cd_attr("id", "buttons");
    b_start = button("start", "|<");
    b_back = button("back", "<");
    cd_text(" ");
    b_play = cd_open_id("button"); cd_attr("id", "play"); cd_text("Play"); cd_name_text(play_txt = cd_id_new()); cd_close();
    b_fwd = button("fwd", ">");
    b_end = button("end", ">|");
    cd_text(" ");
    cd_open("span"); cd_attr("id", "status"); cd_text("No game open."); cd_name_text(status_txt = cd_id_new()); cd_close();
    cd_text(" ");
    cd_open("span"); cd_attr("id", "err"); cd_open("span"); cd_attr("style", "color:#cc0000");
    cd_text(""); cd_name_text(err_txt = cd_id_new());
    cd_close(); cd_close();
    cd_close();
    board = cd_open_id("div"); cd_attr("id", "board"); cd_close();
    cd_on(b_open, CD_EV_CLICK, on_open, nullptr);
    cd_on(field, CD_EV_CHANGE, on_open, nullptr);
    cd_on(b_start, CD_EV_CLICK, on_step, nullptr);
    cd_on(b_back, CD_EV_CLICK, on_step, nullptr);
    cd_on(b_fwd, CD_EV_CLICK, on_step, nullptr);
    cd_on(b_end, CD_EV_CLICK, on_step, nullptr);
    cd_on(b_play, CD_EV_CLICK, on_play, nullptr);
    /* uttt.html?<code>: the game opens at once (anything after an & is the page's own, not the code). */
    uint32_t n = cd_url_read(CD_URL_SEARCH, buf, sizeof buf - 1);
    if (n > 1 && n < sizeof buf - 1) {
        uint32_t k = 1;
        while (k < n && buf[k] != '&') k++;
        if (k > 1) {
            cd_set_str(field, "value", cd_tmp(buf + 1, k - 1));
            open_code(buf + 1, k - 1);
        }
    }
}
