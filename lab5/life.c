/*****************************************************************************
 * life.c
 * Parallelized and optimized implementation of the game of life resides here
 ****************************************************************************/
#include "life.h"
#include "util.h"

#include <assert.h>
#include <stdio.h>

/*****************************************************************************
 * Helper function definitions
 ****************************************************************************/

/**
 * Swapping the two boards only involves swapping pointers, not
 * copying values.
 */
#define SWAP_BOARDS( b1, b2 )  do { \
  char* temp = b1; \
  b1 = b2; \
  b2 = temp; \
} while(0)

#define BOARD( __board, __r, __c )  (__board[(__c) + LDA*(__r)])

void print_window(int r, int c, char *top, char *mid, char *bot) {
    printf("loc: %d %d\n", r, c);
    printf("%d %d %d\n", top[0], top[1], top[2]);
    printf("%d %d %d\n", mid[0], mid[1], mid[2]);
    printf("%d %d %d\n", bot[0], bot[1], bot[2]);
}

/*****************************************************************************
 * Game of life implementation
 ****************************************************************************/
char*
game_of_life (char* outboard, 
	      char* inboard,
	      const int nrows,
	      const int ncols,
	      const int gens_max)
{
//  return sequential_game_of_life(outboard, inboard, nrows, ncols, gens_max);

    /* HINT: in the parallel decomposition, LDA may not be equal to
       nrows! */
    const int LDA = nrows;
    int curgen, i, j;

    for (curgen = 0; curgen < gens_max; curgen++)
    {
        int idx_top_row = nrows - 1;
        int idx_bot_row = 1;
        int idx_right_col = 1;

        char top[3] = {
            inboard[idx_top_row * LDA + (ncols - 1)],
            inboard[idx_top_row * LDA],
            inboard[idx_top_row * LDA + 1],
        };
        char mid[3] = {
            inboard[(ncols - 1)],
            inboard[0],
            inboard[1],
        };
        char bot[3] = {
            inboard[LDA + (ncols - 1)],
            inboard[LDA],
            inboard[LDA + 1],
        };

        /* HINT: you'll be parallelizing these loop(s) by doing a
           geometric decomposition of the output */
        for (i = 0; i < nrows; i++)
        {
            for (j = 0; j < ncols; j++)
            {
#ifdef DEBUG
                printf("top_row: %d, bot_row: %d right_col: %d\n", idx_top_row, idx_bot_row, idx_right_col);
                print_window(i, j, top, mid, bot);
#endif

                const char neighbor_count = 
                    top[0] + top[1] + top[2] +
                    mid[0] + mid[2] + 
                    bot[0] + bot[1] + bot[2];

                BOARD(outboard, i, j) = alivep(neighbor_count, mid[1]);

                idx_right_col = mod(idx_right_col + 1, ncols);

                top[0] = top[1];
                top[1] = top[2];
                top[2] = BOARD(inboard, idx_top_row, idx_right_col);
                mid[0] = mid[1];
                mid[1] = mid[2];
                mid[2] = BOARD(inboard, i, idx_right_col);
                bot[0] = bot[1];
                bot[1] = bot[2];
                bot[2] = BOARD(inboard, idx_bot_row, idx_right_col);
            }
            
            top[0] = mid[0];
            top[1] = mid[1];
            top[2] = BOARD(inboard, i, 1);
            mid[0] = bot[0];
            mid[1] = bot[1];
            mid[2] = BOARD(inboard, idx_bot_row, 1);
            
            idx_top_row = i;
            idx_bot_row = mod(idx_bot_row + 1, nrows);
            bot[0] = BOARD(inboard, idx_bot_row, ncols - 1);
            bot[1] = BOARD(inboard, idx_bot_row, 0);
            bot[2] = BOARD(inboard, idx_bot_row, 1);
        }

        SWAP_BOARDS(outboard, inboard);
    }
    /* 
     * We return the output board, so that we know which one contains
     * the final result (because we've been swapping boards around).
     * Just be careful when you free() the two boards, so that you don't
     * free the same one twice!!! 
     */
    return inboard;
}
