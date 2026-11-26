/* What tools/uttt.mjs expects of demo/uttt, computed by foolish's own code rather than by hand: built natively
 * against a foolish checkout (tools/uttt.mjs does it), it reads a code from argv[1] and prints
 *   ok <n_plies>                      or   bad
 * then, for each ply k from 0 to n_plies, the position after the first k plies:
 *   <k> <81 cells: . X O> <9 blocks: . X O D> <9 backgrounds> <over: . X O D> <turn: X O> <last move or -1>
 * where a block's background is L where the next move may go, W in the winning line, D drawn, and . for none, in that
 * order (demo/uttt/uttt_replay.c, draw_block). Plain text lines, one a ply. */
#include <stdio.h>
#include "uttt_code.h"

int main(int argc, char **argv) {
    UtttGame game, g;
    if (argc < 2 || !uttt_replay_read(argv[1], &game, NULL)) { printf("bad\n"); return 0; }
    printf("ok %d\n", game.n_plies);
    static const char cell[] = ".XO", block[] = ".XOD";
    for (int k = 0; k <= game.n_plies; k++) {
        uttt_init(&g);
        for (int p = 0; p < k; p++) uttt_play(&g, game.move[p]);
        printf("%d ", k);
        for (int i = 0; i < 81; i++) putchar(cell[uttt_cell(&g, i)]);
        putchar(' ');
        for (int b = 0; b < 9; b++) putchar(block[uttt_block(&g, b)]);
        putchar(' ');
        unsigned legal = g.over ? 0 : uttt_legal_blocks(&g);
        int line = uttt_won_line(&g);
        unsigned won = line >= 0 ? uttt_line_mask(line) : 0;
        for (int b = 0; b < 9; b++)
            putchar((legal >> b) & 1u ? 'L' : (won >> b) & 1u ? 'W' : uttt_block(&g, b) == UTTT_DRAW ? 'D' : '.');
        printf(" %c %c %d\n", block[g.over], g.turn == UTTT_X ? 'X' : 'O', k ? game.move[k - 1] : -1);
    }
    return 0;
}
