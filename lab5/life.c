/*****************************************************************************
 * life.c
 * Parallelized and optimized implementation of the game of life resides here
 ****************************************************************************/
#include "life.h"
#include "util.h"

#include <assert.h>
#include <pthread.h>
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

#define BOARD( __board, __r, __c )  (__board[(__c) + size*(__r)])

static void
print_window(int r, int c, char *top, char *mid, char *bot) {
    printf("loc: %d %d\n", r, c);
    printf("%d %d %d\n", top[0], top[1], top[2]);
    printf("%d %d %d\n", mid[0], mid[1], mid[2]);
    printf("%d %d %d\n", bot[0], bot[1], bot[2]);
}

static void
print_args(struct life_thread_args *args) {
    printf("row_start: %d, row_end: %d\n", args->row_start, args->row_end);
}

void*
life_thread_fn(void *argptr) {
    struct life_thread_args *args = argptr;

    pthread_barrier_t *barrier = args->barrier;
    char *outboard = args->outboard;
    char *inboard = args->inboard;
    int size = args->size;
    int gens_max = args->gens_max;
    int row_start = args->row_start;
    int row_end = args->row_end;

    int curgen, i, j;

    for (curgen = 0; curgen < gens_max; curgen++) {
        int idx_top_row = mod(row_start - 1, size);
        int idx_bot_row = mod(row_start + 1, size);
        int idx_right_col = 1;

        char top[3] = {
            BOARD(inboard, idx_top_row, size - 1),
            BOARD(inboard, idx_top_row, 0),
            BOARD(inboard, idx_top_row, 1),
        };
        char mid[3] = {
            BOARD(inboard, row_start, size - 1),
            BOARD(inboard, row_start, 0),
            BOARD(inboard, row_start, 1),
        };
        char bot[3] = {
            BOARD(inboard, idx_bot_row, size - 1),
            BOARD(inboard, idx_bot_row, 0),
            BOARD(inboard, idx_bot_row, 1),
        };

        /* HINT: you'll be parallelizing these loop(s) by doing a
           geometric decomposition of the output */
        for (i = row_start; i < row_end; i++)
        {
            for (j = 0; j < size; j++)
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

                idx_right_col = mod(idx_right_col + 1, size);

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
            idx_bot_row = mod(idx_bot_row + 1, size);
            bot[0] = BOARD(inboard, idx_bot_row, size - 1);
            bot[1] = BOARD(inboard, idx_bot_row, 0);
            bot[2] = BOARD(inboard, idx_bot_row, 1);
        }

        SWAP_BOARDS(outboard, inboard);
        pthread_barrier_wait(barrier);
    }
    /* 
     * We return the output board, so that we know which one contains
     * the final result (because we've been swapping boards around).
     * Just be careful when you free() the two boards, so that you don't
     * free the same one twice!!! 
     */
    return inboard;
}

#define NUM_THREADS 16

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
    pthread_barrier_t barrier;
    pthread_t threads[NUM_THREADS];
    struct life_thread_args args[NUM_THREADS];

    pthread_barrier_init(&barrier, NULL, NUM_THREADS);

    for (int i = 0; i < NUM_THREADS; i++) {
        args[i].barrier = &barrier;
        args[i].inboard = inboard;
        args[i].outboard = outboard;
        args[i].gens_max = gens_max;
        args[i].size = ncols;
        args[i].row_start = (nrows) / NUM_THREADS * i;
        args[i].row_end = (nrows) / NUM_THREADS * (i + 1);

#ifdef DEBUG
        print_args(&args[i]);
#endif
        pthread_create(&threads[i], NULL, life_thread_fn, &(args[i]));
    }

    char *ret;
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], (void**) &(ret));
    }
    return ret;
}
