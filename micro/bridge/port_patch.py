#!/usr/bin/env python3
"""Rewrites a copy of the foolish cdom port's table_app.c for one page arm (the port's checkout is
never touched; the bench builds from the copy).

    port_patch.py <mode> <table_app.c> <out.c>

modes:
  base   only the base rewrite below (arms a, list, b, c, d, f).
  e      the port's style write chains as fused style ops (cdom.h cd_style_begin/num/str/end, arm e):
         a ghost's frame (left, top, transform, filter), a flight's arming (transition, left, top,
         transform), the drag shadow's move (left, top), a hand card's patch (up to six properties),
         and the battle row's glide (transition, transform pairs). The rect reads between the glide's
         writes stay where they are: a fused op only ever holds writes, in the order they were written.
  e_t    the per-frame movers by transform instead of left and top (arm a's cdom): a ghost's position
         joins its transform (translate(dx, dy) from where it was built), and the drag shadow moves by
         a translate from where it was built; no layout per frame for either.
  e_css  one style string per ghost per frame (arm a's cdom): the ghost's whole style attribute, with
         its fixed part, written at once (setAttribute("style")), and the drag shadow's (left, top
         only) the same way. Exactly those properties: in table_app.c a ghost's node is written only
         by its build (cd_attr style: position, left, top, transform, line-height, will-change) and
         ghost_draw (left, top, transform, filter), and the shadow's root only by its build (left,
         top) and render_drag_position (left, top); main() checks both still hold. The numbers are
         C's (4 decimals, cd_fmt_len) where arm a's are JS's (String(v)), so a ghost's or the
         shadow's style text differs from arm a's mid-flight: the one expected difference.
Every mode first applies the base rewrite (all arms, a included): the test build's debug exports
that change the board (app_debug_deal, app_debug_board, app_debug_transfer) end in cd_flush, as every
cdom export does, so an arm that holds operations (list, e) lands them in the same task as arm a.
Every rewrite is an exact text substitution that must match once.
"""
import re
import sys


def sub_once(text, old, new, what):
    if text.count(old) != 1:
        raise SystemExit(f"{what}: anchor found {text.count(old)} times")
    return text.replace(old, new)


GHOST_OLD = """  cd_set_style_num(ghost[g].node, ZS("left"), q * q * ghost[g].fx + 2 * q * p * cx + p * p * ghost[g].tx, CD_UNIT_PX);
  cd_set_style_num(ghost[g].node, ZS("top"), q * q * ghost[g].fy + 2 * q * p * cy + p * p * ghost[g].ty, CD_UNIT_PX);
  static SB s;
  sb_reset(&s);
  sb_s(&s, "translate(-50%, -50%) rotate("); sb_num(&s, 360 * p, CD_UNIT_DEG);
  sb_s(&s, ") scale("); sb_num(&s, 1 + 0.45 * lift, CD_UNIT_NONE); sb_s(&s, ")");
  cd_set_style(ghost[g].node, ZS("transform"), sb_tmp(&s));
  sb_reset(&s);
  sb_s(&s, "drop-shadow(0 "); sb_num(&s, 6 * lift, CD_UNIT_PX); sb_s(&s, " "); sb_num(&s, 8 * lift, CD_UNIT_PX);
  sb_s(&s, " rgba(0,0,0,"); sb_num(&s, 0.45 * lift, CD_UNIT_NONE); sb_s(&s, "))");
  cd_set_style(ghost[g].node, ZS("filter"), sb_tmp(&s));
}"""
GHOST_E = """  cd_style_begin(ghost[g].node);
  cd_style_num(ZS("left"), q * q * ghost[g].fx + 2 * q * p * cx + p * p * ghost[g].tx, CD_UNIT_PX);
  cd_style_num(ZS("top"), q * q * ghost[g].fy + 2 * q * p * cy + p * p * ghost[g].ty, CD_UNIT_PX);
  static SB s;
  sb_reset(&s);
  sb_s(&s, "translate(-50%, -50%) rotate("); sb_num(&s, 360 * p, CD_UNIT_DEG);
  sb_s(&s, ") scale("); sb_num(&s, 1 + 0.45 * lift, CD_UNIT_NONE); sb_s(&s, ")");
  cd_style_str(ZS("transform"), sb_tmp(&s));
  sb_reset(&s);
  sb_s(&s, "drop-shadow(0 "); sb_num(&s, 6 * lift, CD_UNIT_PX); sb_s(&s, " "); sb_num(&s, 8 * lift, CD_UNIT_PX);
  sb_s(&s, " rgba(0,0,0,"); sb_num(&s, 0.45 * lift, CD_UNIT_NONE); sb_s(&s, "))");
  cd_style_str(ZS("filter"), sb_tmp(&s));
  cd_style_end();
}"""
GHOST_T = """  double x = q * q * ghost[g].fx + 2 * q * p * cx + p * p * ghost[g].tx;
  double y = q * q * ghost[g].fy + 2 * q * p * cy + p * p * ghost[g].ty;
  static SB s;
  sb_reset(&s); /* left and top stay where the ghost was built (fx, fy): the move is a translate */
  sb_s(&s, "translate("); sb_num(&s, x - ghost[g].fx, CD_UNIT_PX); sb_s(&s, ", "); sb_num(&s, y - ghost[g].fy, CD_UNIT_PX);
  sb_s(&s, ") translate(-50%, -50%) rotate("); sb_num(&s, 360 * p, CD_UNIT_DEG);
  sb_s(&s, ") scale("); sb_num(&s, 1 + 0.45 * lift, CD_UNIT_NONE); sb_s(&s, ")");
  cd_set_style(ghost[g].node, ZS("transform"), sb_tmp(&s));
  sb_reset(&s);
  sb_s(&s, "drop-shadow(0 "); sb_num(&s, 6 * lift, CD_UNIT_PX); sb_s(&s, " "); sb_num(&s, 8 * lift, CD_UNIT_PX);
  sb_s(&s, " rgba(0,0,0,"); sb_num(&s, 0.45 * lift, CD_UNIT_NONE); sb_s(&s, "))");
  cd_set_style(ghost[g].node, ZS("filter"), sb_tmp(&s));
}"""
GHOST_CSS = """  static SB s;
  sb_reset(&s); /* the whole style attribute, in the order arm a's writes leave it: the build's (position, left, top,
                 transform, line-height, will-change), then filter, the one property a frame adds */
  sb_s(&s, "position: absolute; ");
  sb_px(&s, "left: ", q * q * ghost[g].fx + 2 * q * p * cx + p * p * ghost[g].tx);
  sb_px(&s, "top: ", q * q * ghost[g].fy + 2 * q * p * cy + p * p * ghost[g].ty);
  sb_s(&s, "transform: translate(-50%, -50%) rotate("); sb_num(&s, 360 * p, CD_UNIT_DEG);
  sb_s(&s, ") scale("); sb_num(&s, 1 + 0.45 * lift, CD_UNIT_NONE); sb_s(&s, "); ");
  sb_s(&s, "line-height: 0; will-change: transform, left, top; ");
  sb_s(&s, "filter: drop-shadow(0 "); sb_num(&s, 6 * lift, CD_UNIT_PX); sb_s(&s, " "); sb_num(&s, 8 * lift, CD_UNIT_PX);
  sb_s(&s, " rgba(0,0,0,"); sb_num(&s, 0.45 * lift, CD_UNIT_NONE); sb_s(&s, "));");
  cd_set_attr(ghost[g].node, S_STYLE, sb_tmp(&s));
}"""

DRAG_OLD = """  cd_set_style_num(shadow.root, ZS("left"), shadow_shown.x = DRAG.x, CD_UNIT_PX);
  cd_set_style_num(shadow.root, ZS("top"), (shadow_shown.y = DRAG.y) - 40, CD_UNIT_PX);
}"""
DRAG_E = """  cd_style_begin(shadow.root);
  cd_style_num(ZS("left"), shadow_shown.x = DRAG.x, CD_UNIT_PX);
  cd_style_num(ZS("top"), (shadow_shown.y = DRAG.y) - 40, CD_UNIT_PX);
  cd_style_end();
}"""
DRAG_T = """  static SB s; /* left and top stay where the shadow was built: the move is a translate, before the class's own */
  sb_reset(&s);
  sb_s(&s, "translate("); sb_num(&s, (shadow_shown.x = DRAG.x) - shadow_base_x, CD_UNIT_PX); sb_s(&s, ", ");
  sb_num(&s, (shadow_shown.y = DRAG.y) - shadow_base_y, CD_UNIT_PX); sb_s(&s, ") translate(-50%, -50%)");
  cd_set_style(shadow.root, ZS("transform"), sb_tmp(&s));
}"""
DRAG_CSS = """  static SB s; /* its style attribute holds only left and top */
  sb_reset(&s);
  sb_px(&s, "left: ", shadow_shown.x = DRAG.x);
  sb_px(&s, "top: ", (shadow_shown.y = DRAG.y) - 40);
  cd_set_attr(shadow.root, S_STYLE, sb_tmp(&s));
}"""
DRAG_BASE_OLD = """  shadow_shown.x = DRAG.x;
  shadow_shown.y = DRAG.y;
"""
DRAG_BASE_T = """  shadow_shown.x = shadow_base_x = DRAG.x;
  shadow_shown.y = shadow_base_y = DRAG.y;
"""
DRAG_DECL_OLD = "static void render_drag_position(void) {"
DRAG_DECL_T = "static int shadow_base_x, shadow_base_y; /* where the shadow was built (e_t) */\nstatic void render_drag_position(void) {"

FLIGHT_OLD = """    cd_set_style(flight[i].node, ZS("transition"), sb_tmp(&s));
    cd_set_style_num(flight[i].node, ZS("left"), flight[i].x, CD_UNIT_PX);
    cd_set_style_num(flight[i].node, ZS("top"), flight[i].y, CD_UNIT_PX);
    sb_reset(&s);
    sb_flight_transform(&s, 1.8, flight[i].angle);
    cd_set_style(flight[i].node, ZS("transform"), sb_tmp(&s));
"""
FLIGHT_E = """    cd_style_begin(flight[i].node);
    cd_style_str(ZS("transition"), sb_tmp(&s));
    cd_style_num(ZS("left"), flight[i].x, CD_UNIT_PX);
    cd_style_num(ZS("top"), flight[i].y, CD_UNIT_PX);
    sb_reset(&s);
    sb_flight_transform(&s, 1.8, flight[i].angle);
    cd_style_str(ZS("transform"), sb_tmp(&s));
    cd_style_end();
"""
GLIDE = [
    ("""      if (cell_glided[aid]) { cd_set_style(battle_cell[aid], ZS("transition"), ZS("none")); cd_set_style(battle_cell[aid], ZS("transform"), ZS("")); cell_glided[aid] = 0; }""",
     """      if (cell_glided[aid]) { cd_style_begin(battle_cell[aid]); cd_style_str(ZS("transition"), ZS("none")); cd_style_str(ZS("transform"), ZS("")); cd_style_end(); cell_glided[aid] = 0; }"""),
    ("""    cd_set_style(battle_cell[aid], ZS("transition"), ZS("none"));
    cd_set_style(battle_cell[aid], ZS("transform"), sb_tmp(&t));""",
     """    cd_style_begin(battle_cell[aid]);
    cd_style_str(ZS("transition"), ZS("none"));
    cd_style_str(ZS("transform"), sb_tmp(&t));
    cd_style_end();"""),
    ("""    cd_set_style(battle_cell[moved[k]], ZS("transition"), sb_tmp(&t));
    cd_set_style(battle_cell[moved[k]], ZS("transform"), ZS(""));""",
     """    cd_style_begin(battle_cell[moved[k]]);
    cd_style_str(ZS("transition"), sb_tmp(&t));
    cd_style_str(ZS("transform"), ZS(""));
    cd_style_end();"""),
]


def hand_patch_e(c):
    start = c.index("static void hand_patch(void *ctx, uint32_t i, cd_item *it) {")
    end = c.index("\n}\n", start)
    body = c[start:end]
    first = '  if (r->x != shown[cid].x)'
    last_style = '  if (dim != shown[cid].dim)'
    if body.count(first) != 1 or body.count(last_style) != 1:
        raise SystemExit("hand_patch: anchors")
    body = body.replace(first, "  cd_style_begin(it->node);\n" + first)
    body = re.sub(r"cd_set_style_num\(it->node, ", "cd_style_num(", body)
    body = re.sub(r"cd_set_style\(it->node, ", "cd_style_str(", body)
    line_end = body.index("\n", body.index(last_style))
    body = body[:line_end] + "\n  cd_style_end();" + body[line_end:]
    if body.count("cd_style_num(") != 4 or body.count("cd_style_str(") != 2:
        raise SystemExit("hand_patch: expected 4 numbers and 2 strings")
    return c[:start] + body + c[end:]


BASE = [
    ('EXPORT("app_debug_deal") void app_debug_deal(void) { new_game(); }',
     'EXPORT("app_debug_deal") void app_debug_deal(void) { new_game(); cd_flush(); }'),
]


def base(c):
    for old, new in BASE:
        c = sub_once(c, old, new, "base")
    for name in ("app_debug_board", "app_debug_transfer"):  # their bodies end "  bot_gen++;\n...}\n": flush before the brace
        start = c.index(f'EXPORT("{name}")')
        end = c.index("\n}\n", start)
        c = c[:end] + "\n  cd_flush();" + c[end:]
    return c


def check_writers(c):
    """The e_css rewrite holds only while these are a ghost's and the shadow's only style writers."""
    if len(re.findall(r"cd_set_style\w*\(ghost\[g\]\.node", c)) != 4 or c.count("cd_name_open(ghost[g].node)") != 1:
        raise SystemExit("e_css: a ghost's node has other writers now")
    if len(re.findall(r"cd_set_\w*\(shadow\.root", c)) != 2:
        raise SystemExit("e_css: the shadow's root has other writers now")


def main():
    mode, src, out = sys.argv[1:4]
    c = base(open(src).read())
    if mode == "base":
        pass
    elif mode == "e":
        c = sub_once(c, GHOST_OLD, GHOST_E, "ghost")
        c = sub_once(c, DRAG_OLD, DRAG_E, "drag")
        c = sub_once(c, FLIGHT_OLD, FLIGHT_E, "flights_arm")
        for k, (old, new) in enumerate(GLIDE):
            c = sub_once(c, old, new, f"glide {k}")
        c = hand_patch_e(c)
    elif mode == "e_t":
        c = sub_once(c, GHOST_OLD, GHOST_T, "ghost")
        c = sub_once(c, DRAG_OLD, DRAG_T, "drag")
        c = sub_once(c, DRAG_BASE_OLD, DRAG_BASE_T, "drag base")
        c = sub_once(c, DRAG_DECL_OLD, DRAG_DECL_T, "drag decl")
    elif mode == "e_css":
        check_writers(c)
        c = sub_once(c, GHOST_OLD, GHOST_CSS, "ghost")
        c = sub_once(c, DRAG_OLD, DRAG_CSS, "drag")
    else:
        raise SystemExit(__doc__)
    open(out, "w").write(c)


if __name__ == "__main__":
    main()
