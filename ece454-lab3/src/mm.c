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
#define CHUNKSIZE   (1<<7)      /* initial heap size (bytes) */

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
#define FTRP(bp)        ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)

/* Given block ptr bp, compute address of next and previous blocks */
#define NEXT_BLKP(bp) ((char *)(bp) + GET_SIZE(((char *)(bp) - WSIZE)))
#define PREV_BLKP(bp) ((char *)(bp) - GET_SIZE(((char *)(bp) - DSIZE)))

// Given free block ptr bp, get next and prev ptr address
#define NEXT_FREE_BLKP(bp) ((uint8_t*) (bp))
#define PREV_FREE_BLKP(bp) ((uint8_t*) (bp) + WSIZE)

uint8_t *heap_list; // free list root
uint8_t *heap_top;

/**********************************************************
 * mm_init
 * Initialize the heap, including "allocation" of the
 * prologue and epilogue
 **********************************************************/
 int mm_init(void) {
     if ((heap_top = mem_sbrk(4*WSIZE)) == (void *)-1)
         return -1;
     PUT(heap_top, 0);                         // alignment padding
     PUT(heap_top + (1 * WSIZE), PACK(0, 1));   // prologue header
     PUT(heap_top + (2 * WSIZE), PACK(0, 1));   // prologue footer
     PUT(heap_top + (3 * WSIZE), PACK(0, 1));    // epilogue header

     return 0;
 }

/**********************************************************
 * coalesce
 * Covers the 4 cases discussed in the text:
 * - both neighbours are allocated
 * - the next block is available for coalescing
 * - the previous block is available for coalescing
 * - both neighbours are available for coalescing
 **********************************************************/
void *coalesce(void *bp)
{
    size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(bp)));
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
    size_t size = GET_SIZE(HDRP(bp));

    if (prev_alloc && next_alloc) {       /* Case 1 */
        return bp;
    }

    else if (prev_alloc && !next_alloc) { /* Case 2 */
        size += GET_SIZE(HDRP(NEXT_BLKP(bp)));
        PUT(HDRP(bp), PACK(size, 0));
        PUT(FTRP(bp), PACK(size, 0));
        return (bp);
    }

    else if (!prev_alloc && next_alloc) { /* Case 3 */
        size += GET_SIZE(HDRP(PREV_BLKP(bp)));
        PUT(FTRP(bp), PACK(size, 0));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        return (PREV_BLKP(bp));
    }

    else {            /* Case 4 */
        size += GET_SIZE(HDRP(PREV_BLKP(bp)))  +
            GET_SIZE(FTRP(NEXT_BLKP(bp)))  ;
        PUT(HDRP(PREV_BLKP(bp)), PACK(size,0));
        PUT(FTRP(NEXT_BLKP(bp)), PACK(size,0));
        return (PREV_BLKP(bp));
    }
}

/**********************************************************
 * extend_heap
 * Extend the heap by "words" words, maintaining alignment
 * requirements of course. Free the former epilogue block
 * and reallocate its new header
 **********************************************************/
uint8_t *extend_heap(uint64_t words) {
    /* Allocate an even number of words to maintain alignments */
    uint64_t size = (words % 2) ? (words+1) * WSIZE : words * WSIZE;
    uint8_t *block = mem_sbrk(size);
    if (block == (uint8_t*) -1)
        return NULL;

    // Assume that if previous block is free, the block before it is not
    // i.e. the original free block was coalesced
    uint8_t *prev_contig = block - 2 * WSIZE;

    // check flag from highest addressed block from before sbrk'ing
    bool coalescable = !GET_ALLOC(prev_contig);

    if (coalescable) {
        // TODO: Not validated yet
        uint64_t pc_size = GET_SIZE(prev_contig);
        uint64_t comb_size = pc_size + size;

        block = prev_contig - pc_size;

        // Need to stop the prev free block from pointing to this block
        uint64_t *prev_free = (uint64_t*) GET(PREV_FREE_BLKP(block));
        if (prev_free != NULL) {
            PUT(NEXT_FREE_BLKP(prev_free), (uint64_t) NULL);
        }

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
uint8_t *find_fit(uint64_t req_size) {
    uint8_t *cur = heap_list;
    while (cur != NULL) {
        uint64_t size = GET(HDRP(cur)); // GET is equivalent to GET_SIZE since alloc bit is zero
        if (req_size <= size) {
            return cur;
        }

        // TODO: Not validated yet
        cur = (uint8_t*) GET(NEXT_FREE_BLKP(cur));
    }

    return NULL;
}

/**********************************************************
 * place
 * Mark the block as allocated
 **********************************************************/
void place(uint8_t *bp, uint64_t size) {
    /* Get the current block size */
    uint64_t bsize = GET_SIZE(HDRP(bp));
    // TODO: Add splitting

    PUT(HDRP(bp), PACK(bsize, 1));
    PUT(FTRP(bp), PACK(bsize, 1));

    // Remove this block from the free list
    uint64_t *next = (uint64_t*) GET(NEXT_FREE_BLKP(bp));
    uint64_t *prev = (uint64_t*) GET(PREV_FREE_BLKP(bp));
    if (next != NULL)
        PUT(PREV_FREE_BLKP(next), (uint64_t) prev); // Fix prev pointer of next block
    if (prev != NULL)
        PUT(NEXT_FREE_BLKP(prev), (uint64_t) next); // Fix next pointer of prev block
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
    PUT(HDRP(bp), PACK(size,0));
    PUT(FTRP(bp), PACK(size,0));

    // TODO: Coalescing
}


/**********************************************************
 * mm_malloc
 * Allocate a block of size bytes.
 * The type of search is determined by find_fit
 * The decision of splitting the block, or not is determined
 *   in place(..)
 * If no block satisfies the request, the heap is extended
 **********************************************************/
void *mm_malloc(size_t req_size)
{
    uint64_t adj_size; /* adjusted block size */
    uint64_t ext_size; /* amount to extend heap if no fit */
    uint8_t *block;

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
    ext_size = MAX(adj_size, CHUNKSIZE);
    if ((block = extend_heap(ext_size/WSIZE)) == NULL)
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
int mm_check(void){
  return 1;
}
