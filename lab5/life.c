/*****************************************************************************
 * life.c
 * Parallelized and optimized implementation of the game of life resides here
 ****************************************************************************/
#include "life.h"
#include "neighbours.h"
#include "util.h"

#include <assert.h>
#include <pthread.h>
#include <stdint.h>
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
print_neighbours(int top, int bot, int r, int c, uint16_t neighbours) {
    static pthread_mutex_t stdout_mutex = PTHREAD_MUTEX_INITIALIZER;

    pthread_mutex_lock(&stdout_mutex);
    printf("loc: %d %d\n", r, c);
    printf("top: %d bot: %d\n", top, bot);
    printf("%d %d %d\n",
            (neighbours & (1 << 8)) != 0,
            (neighbours & (1 << 5)) != 0,
            (neighbours & (1 << 2)) != 0);
    printf("%d %d %d\n",
            (neighbours & (1 << 7)) != 0,
            (neighbours & (1 << 4)) != 0,
            (neighbours & (1 << 1)) != 0);
    printf("%d %d %d\n",
            (neighbours & (1 << 6)) != 0,
            (neighbours & (1 << 3)) != 0,
            (neighbours & (1 << 0)) != 0);
    pthread_mutex_unlock(&stdout_mutex);
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
        for (i = row_start; i < row_end; i++)
        {
            int idx_top_row = mod(i-1, size);
            int idx_bot_row = mod(i+1, size);

            uint16_t neighbours =
                    BOARD(inboard, idx_top_row, size - 1) << 5 |
                    BOARD(inboard, i, size - 1) << 4 |
                    BOARD(inboard, idx_bot_row, size - 1) << 3 |
                    BOARD(inboard, idx_top_row, 0) << 2 |
                    BOARD(inboard, i, 0) << 1 |
                    BOARD(inboard, idx_bot_row, 0) << 0;

            for (j = 0; j < size; j++)
            {
                neighbours = ((neighbours << 3) & 0x1FF) |
                    BOARD(inboard, idx_top_row, mod(j+1, size)) << 2 |
                    BOARD(inboard, i, mod(j+1, size)) << 1 |
                    BOARD(inboard, idx_bot_row, mod(j+1, size)) << 0;

#ifdef DEBUG
                print_neighbours(idx_top_row, idx_bot_row, i, j, neighbours);
#endif

                BOARD(outboard, i, j) = neighbours_lut[neighbours];
            }
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

        pthread_create(&threads[i], NULL, life_thread_fn, &(args[i]));
    }

    char *ret;
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], (void**) &(ret));
    }
    return ret;
}
