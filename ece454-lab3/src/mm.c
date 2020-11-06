/*
 * This implementation replicates the implicit list implementation
 * provided in the textbook
 * "Computer Systems - A Programmer's Perspective"
 * Blocks are never coalesced or reused.
 * Realloc is implemented directly using mm_malloc and mm_free.
 *
 * NOTE TO STUDENTS: Replace this header comment with your own header
 * comment that gives a high level description of your solution.
 */
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>
#include <stdint.h>

#include "mm.h"
#include "memlib.h"

void insert_free(uint64_t *free_block, uint64_t **list_root);

/*********************************************************
 * NOTE TO STUDENTS: Before you do anything else, please
 * provide your team information in the following struct.
 ********************************************************/
team_t team = {
    /* Team name */
    "USE-AFTER-FREE",
    /* First member's full name */
    "Benjamin Cheng",
    /* First member's email address */
    "benjamin.cheng@mail.utoronto.ca",
    /* Second member's full name (do not modify this as this is an individual lab) */
    "",
    /* Second member's email address (do not modify this as this is an individual lab)*/
    ""
};

/*************************************************************************
 * Basic Constants and Macros
 * You are not required to use these macros but may find them helpful.
*************************************************************************/
#define WSIZE       sizeof(void *)            /* word size (bytes) */
#define DSIZE       (2 * WSIZE)            /* doubleword size (bytes) */
#define MINBLOCKSIZE   (4 * WSIZE)      /* header, next ptr, prev ptr, footer */

#define MAX(x,y) ((x) > (y)?(x) :(y))

/* Pack a size and allocated bit into a word */
#define PACK(size, alloc) ((size) | (alloc))

/* Read and write a word at address p */
#define GET(p)          (*(uint64_t *)(p))
#define PUT(p,val)      (*(uint64_t *)(p) = (val))

/* Read the size and allocated fields from address p */
#define GET_SIZE(p)     (GET(p) & ~(DSIZE - 1))
#define GET_ALLOC(p)    (GET(p) & 0x1)

/* Given block ptr bp, compute address of its header and footer */
#define HDRP(bp)        ((char *)(bp) - WSIZE)
#define FTRP(bp)        ((char *)(bp) + GET_SIZE(HDRP(bp)))

/* Given block ptr bp, compute address of the adjacent (in memory) blocks */
#define NEXT_BLKP(bp) ((char *)(bp) + GET_SIZE((char *)(bp) - WSIZE) + 2 * WSIZE)
#define PREV_BLKP(bp) ((char *)(bp) - GET_SIZE((char *)(bp) - DSIZE))

// Given free block ptr bp, get next and prev ptr address
#define NEXT_FREE_BLKP(bp) ((uint8_t*) (bp))
#define PREV_FREE_BLKP(bp) ((uint8_t*) (bp) + WSIZE)

#define SEGLIST0_SIZE 32 // MINBLOCKSIZE
#define SEGLIST1_SIZE 36
#define SEGLIST2_SIZE 40
#define SEGLIST3_SIZE 48
#define SEGLIST4_SIZE 64
#define SEGLIST5_SIZE 96
#define SEGLIST6_SIZE 128
#define SEGLIST7_SIZE 256
#define SEGLIST8_SIZE 512
#define SEGLIST9_SIZE 1024
#define SEGLIST10_SIZE 2048
#define SEGLIST11_SIZE 4096

uint64_t *segroots[13];

static int seglist_idx(uint64_t size) {
    if (size < SEGLIST0_SIZE)
        return 0;
    if (size < SEGLIST1_SIZE)
        return 1;
    if (size < SEGLIST2_SIZE)
        return 2;
    if (size < SEGLIST3_SIZE)
        return 3;
    if (size < SEGLIST4_SIZE)
        return 4;
    if (size < SEGLIST5_SIZE)
        return 5;
    if (size < SEGLIST6_SIZE)
        return 6;
    if (size < SEGLIST7_SIZE)
        return 7;
    if (size < SEGLIST8_SIZE)
        return 8;
    if (size < SEGLIST9_SIZE)
        return 9;
    if (size < SEGLIST10_SIZE)
        return 10;
    if (size < SEGLIST11_SIZE)
        return 11;
    return 12;
}

/**********************************************************
 * mm_init
 * Initialize the heap, including "allocation" of the
 * prologue and epilogue
 **********************************************************/
 int mm_init(void) {
     uint64_t *heap_top;
     if ((heap_top = mem_sbrk(4*WSIZE)) == (void *)-1)
         return -1;

     PUT(heap_top, 0);                         // alignment padding
     PUT(heap_top + 1, PACK(0, 1));   // prologue header
     PUT(heap_top + 2, PACK(0, 1));   // prologue footer
     PUT(heap_top + 3, PACK(0, 1));    // epilogue header

     for (int i = 0; i < 13; i++) {
         segroots[i] = NULL;
     }

     return 0;
 }

// splits a free block into two,
// allocating the block starting at block.
void split_alloc(uint64_t *block_a, uint64_t size_a) {
    uint64_t comb_size = GET_SIZE(HDRP(block_a));
    uint64_t size_b = comb_size - size_a - DSIZE;

    uint64_t *next = (uint64_t*) GET(NEXT_FREE_BLKP(block_a));
    uint64_t *prev = (uint64_t*) GET(PREV_FREE_BLKP(block_a));

    PUT(HDRP(block_a), PACK(size_a, 1));
    PUT(FTRP(block_a), PACK(size_a, 1));

    uint64_t *block_b = (uint64_t*) NEXT_BLKP(block_a);
    PUT(HDRP(block_b), PACK(size_b, 0));
    PUT(FTRP(block_b), PACK(size_b, 0));

    // remove block_a from list
    if (prev != NULL)
        PUT(NEXT_FREE_BLKP(prev), (uint64_t) next);
    else
        segroots[seglist_idx(comb_size)] = next;
    if (next != NULL)
        PUT(PREV_FREE_BLKP(next), (uint64_t) prev);

    insert_free(block_b, &segroots[seglist_idx(size_b)]);
}

/**********************************************************
 * extend_heap
 * Extend the heap by "words" words, maintaining alignment
 * requirements of course. Free the former epilogue block
 * and reallocate its new header
 **********************************************************/
uint64_t *extend_heap(uint64_t req_size) {
    uint8_t *pc_footer = mem_heap_hi() + 1 - 2 * WSIZE;

    // Assume that if previous block is free, the block before it is not
    // i.e. the original free block was coalesced
    bool coalescable = !GET_ALLOC(pc_footer);
    uint64_t pc_size = GET_SIZE(pc_footer);

    uint64_t words;
    if (!coalescable) {
        uint64_t ext_size = MAX(req_size, MINBLOCKSIZE);
        words = ext_size / WSIZE;
    } else {
        uint64_t ext_size = MAX(req_size - pc_size - DSIZE, MINBLOCKSIZE);
        words = ext_size / WSIZE;
    }

    /* Allocate an even number of words to maintain alignments */
    uint64_t size = (words % 2) ? (words+1) * WSIZE : words * WSIZE;
    uint64_t *block = mem_sbrk(size);
    if (block == (uint64_t*) -1)
        return NULL;

    size -= DSIZE; // We don't include footer and epilogue in size

    if (coalescable) {
        uint64_t comb_size = pc_size + size + DSIZE; // DSIZE for the internal footer and header

        block = pc_footer - pc_size;

        // Remove this block from the free list
        uint64_t *prev_free = (uint64_t*) GET(PREV_FREE_BLKP(block));
        uint64_t *next_free = (uint64_t*) GET(NEXT_FREE_BLKP(block));
        if (prev_free != NULL)
            PUT(NEXT_FREE_BLKP(prev_free), (uint64_t) next_free);
        else
            segroots[seglist_idx(pc_size)] = next_free;
        if (next_free != NULL)
            PUT(PREV_FREE_BLKP(next_free), (uint64_t) prev_free);

        PUT(HDRP(block), PACK(comb_size, 1)); // block header
        PUT(FTRP(block), PACK(comb_size, 1)); // block footer
        PUT(HDRP(NEXT_BLKP(block)), PACK(0, 1)); // new epi header

        return block;
    }
    
    /* Initialize free block header/footer and the epilogue header */
    PUT(HDRP(block), PACK(size, 1));                // block header, overwrites old epilogue
    PUT(FTRP(block), PACK(size, 1));                // block footer
    PUT(HDRP(NEXT_BLKP(block)), PACK(0, 1));        // new epilogue header

    return block;
}


/**********************************************************
 * find_fit
 * Traverse the heap searching for a block to fit asize
 * Return NULL if no free blocks can handle that size
 * Assumed that asize is aligned
 **********************************************************/
uint64_t *find_fit(uint64_t req_size) {
    for (int idx = seglist_idx(req_size); idx < 13; idx++) {
        uint64_t *cur = segroots[idx];
        while (cur != NULL) {
            uint64_t size = GET(HDRP(cur)); // GET is equivalent to GET_SIZE since alloc bit is zero
            if (req_size <= size) {
                return cur;
            }

            cur = (uint64_t*) GET(NEXT_FREE_BLKP(cur));
        }
    }

    return NULL;
}

/**********************************************************
 * place
 * Mark the block as allocated
 **********************************************************/
void place(uint64_t *bp, uint64_t size) {
    /* Get the current block size */
    uint64_t bsize = GET_SIZE(HDRP(bp));
    if (bsize - size > MINBLOCKSIZE) {
        split_alloc((uint64_t*) bp, size);
        return;
    }

    PUT(HDRP(bp), PACK(bsize, 1));
    PUT(FTRP(bp), PACK(bsize, 1));

    // Remove this block from the free list
    uint64_t *next = (uint64_t*) GET(NEXT_FREE_BLKP(bp));
    uint64_t *prev = (uint64_t*) GET(PREV_FREE_BLKP(bp));
    if (next != NULL)
        PUT(PREV_FREE_BLKP(next), (uint64_t) prev); // Fix prev pointer of next block
    if (prev != NULL)
        PUT(NEXT_FREE_BLKP(prev), (uint64_t) next); // Fix next pointer of prev block
    else
        segroots[seglist_idx(bsize)] = next;
}

void insert_free(uint64_t *free_block, uint64_t **list_root) {
    uint64_t *cur = *list_root;
    *list_root = free_block;

    PUT(NEXT_FREE_BLKP(free_block), cur);
    PUT(PREV_FREE_BLKP(free_block), NULL);
    if (cur != NULL)
        PUT(PREV_FREE_BLKP(cur), free_block);
}

/**********************************************************
 * mm_free
 * Free the block and coalesce with neighbouring blocks
 **********************************************************/
void mm_free(void *bp) {
    if (bp == NULL) {
        return;
    }

    uint64_t size = GET_SIZE(HDRP(bp));
    // check next contiguous block for coalescing
    uint64_t *nc_block = bp + size + DSIZE;
    bool coalesce_next = !GET_ALLOC(HDRP(nc_block));
    // check prev contiguous block for coalescing
    uint64_t *pc_block = bp - DSIZE - GET_SIZE(bp - DSIZE);
    bool coalesce_prev = !GET_ALLOC(HDRP(pc_block));

    if (coalesce_next && coalesce_prev) {
        uint64_t pc_size = GET_SIZE(HDRP(pc_block));
        uint64_t nc_size = GET_SIZE(HDRP(nc_block));
        size += pc_size + nc_size + 2 * DSIZE;

        // update size in header and footer
        PUT(HDRP(pc_block), PACK(size,0));
        PUT(FTRP(pc_block), PACK(size,0));

        uint64_t *pc_prev = (uint64_t*) GET(PREV_FREE_BLKP(pc_block));
        uint64_t *pc_next = (uint64_t*) GET(NEXT_FREE_BLKP(pc_block));

        // Remove pc from its list
        if (pc_prev != NULL)
            PUT(NEXT_FREE_BLKP(pc_prev), (uint64_t) pc_next);
        else
            segroots[seglist_idx(pc_size)] = pc_next;
        if (pc_next != NULL)
            PUT(PREV_FREE_BLKP(pc_next), (uint64_t) pc_prev);

        uint64_t *nc_prev = (uint64_t*) GET(PREV_FREE_BLKP(nc_block));
        uint64_t *nc_next = (uint64_t*) GET(NEXT_FREE_BLKP(nc_block));

        // Remove nc from its list
        if (nc_prev != NULL)
            PUT(NEXT_FREE_BLKP(nc_prev), (uint64_t) nc_next);
        else
            segroots[seglist_idx(nc_size)] = nc_next;
        if (nc_next != NULL)
            PUT(PREV_FREE_BLKP(nc_next), (uint64_t) nc_prev);

        insert_free(pc_block, &segroots[seglist_idx(size)]);
    } else if (coalesce_next) {
        uint64_t nc_size = GET_SIZE(HDRP(nc_block));
        size += nc_size + DSIZE;

        // update size in header and footer
        PUT(HDRP(bp), PACK(size,0));
        PUT(FTRP(bp), PACK(size,0));

        uint64_t *nc_prev = (uint64_t*) GET(PREV_FREE_BLKP(nc_block));
        uint64_t *nc_next = (uint64_t*) GET(NEXT_FREE_BLKP(nc_block));

        // Remove nc from its list
        if (nc_prev != NULL)
            PUT(NEXT_FREE_BLKP(nc_prev), (uint64_t) nc_next);
        else
            segroots[seglist_idx(nc_size)] = nc_next;
        if (nc_next != NULL)
            PUT(PREV_FREE_BLKP(nc_next), (uint64_t) nc_prev);

        insert_free(bp, &segroots[seglist_idx(size)]);
    } else if (coalesce_prev) {
        uint64_t pc_size = GET_SIZE(HDRP(pc_block));
        size += pc_size + DSIZE;

        // update size in header and footer
        PUT(HDRP(pc_block), PACK(size,0));
        PUT(FTRP(pc_block), PACK(size,0));

        uint64_t *pc_prev = (uint64_t*) GET(PREV_FREE_BLKP(pc_block));
        uint64_t *pc_next = (uint64_t*) GET(NEXT_FREE_BLKP(pc_block));

        // Remove pc from its list
        if (pc_prev != NULL)
            PUT(NEXT_FREE_BLKP(pc_prev), (uint64_t) pc_next);
        else
            segroots[seglist_idx(pc_size)] = pc_next;
        if (pc_next != NULL)
            PUT(PREV_FREE_BLKP(pc_next), (uint64_t) pc_prev);

        insert_free(pc_block, &segroots[seglist_idx(size)]);
    } else {
        // unset alloc bit
        PUT(HDRP(bp), PACK(size,0));
        PUT(FTRP(bp), PACK(size,0));

        insert_free(bp, &segroots[seglist_idx(size)]);
    }
}


/**********************************************************
 * mm_malloc
 * Allocate a block of size bytes.
 * The type of search is determined by find_fit
 * The decision of splitting the block, or not is determined
 *   in place(..)
 * If no block satisfies the request, the heap is extended
 **********************************************************/
void *mm_malloc(size_t req_size) {
    uint64_t adj_size; /* adjusted block size */
    uint64_t *block;

    /* Ignore spurious requests */
    if (req_size == 0)
        return NULL;

    /* Adjust block size to include overhead and alignment reqs. */
    if (req_size <= DSIZE)
        adj_size = 2 * DSIZE;
    else
        adj_size = DSIZE * ((req_size + (DSIZE) + (DSIZE-1))/ DSIZE);

    /* Search the free list for a fit */
    if ((block = find_fit(adj_size)) != NULL) {
        place(block, adj_size);
        return block;
    }

    /* No fit found. Get more memory and place the block */
    if ((block = extend_heap(adj_size)) == NULL)
        return NULL;

    return block;
}

/**********************************************************
 * mm_realloc
 * Implemented simply in terms of mm_malloc and mm_free
 *********************************************************/
void *mm_realloc(void *ptr, size_t size)
{
    /* If size == 0 then this is just free, and we return NULL. */
    if(size == 0){
      mm_free(ptr);
      return NULL;
    }
    /* If oldptr is NULL, then this is just malloc. */
    if (ptr == NULL)
      return (mm_malloc(size));

    void *oldptr = ptr;
    void *newptr;
    size_t copySize;

    newptr = mm_malloc(size);
    if (newptr == NULL)
      return NULL;

    /* Copy the old data. */
    copySize = GET_SIZE(HDRP(oldptr));
    if (size < copySize)
      copySize = size;
    memcpy(newptr, oldptr, copySize);
    mm_free(oldptr);
    return newptr;
}

/**********************************************************
 * mm_check
 * Check the consistency of the memory heap
 * Return nonzero if the heap is consistant.
 *********************************************************/
int mm_check(void) {
    /*
    // Check if free list is in ascending order
    uint64_t *cur = (uint64_t*) heap_list;
    uint64_t *prev = NULL;
    while (cur != NULL) {
        if (prev != (uint64_t*) GET(PREV_FREE_BLKP(cur))) // prev pointer should be prev
            return 0;

        prev = cur;
        cur = (uint64_t*) GET(NEXT_FREE_BLKP(cur));
    }
    */

    return 0;
}
