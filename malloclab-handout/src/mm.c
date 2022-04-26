/*
 * mm-naive.c - The fastest, least memory-efficient malloc package.
 *
 * In this naive approach, a block is allocated by simply incrementing
 * the brk pointer.  A block is pure payload. There are no headers or
 * footers.  Blocks are never coalesced or reused. Realloc is
 * implemented directly using mm_malloc and mm_free.
 *
 * NOTE TO STUDENTS: Replace this header comment with your own header
 * comment that gives a high level description of your solution.
 */
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>

#include "mm.h"
#include "memlib.h"

/*********************************************************
 * NOTE TO STUDENTS: Before you do anything else, please
 * provide your team information in the following struct.
 ********************************************************/

/* single word (4) or double word (8) alignment */
#define ALIGNMENT 8

/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(size) (((size) + (ALIGNMENT-1)) & ~0x7)


#define SIZE_T_SIZE (ALIGN(sizeof(size_t)))

#define WSIZE 4
#define DSIZE 8
#define CHUNKSIZE (1 << 12)
#define INIT_CHUNKSIZE (1 << 9)
#define SEG_LIST_MAX 20

#define MAX(x, y) ((x) > (y) ? (x) : (y))
#define PACK(size, alloc) ((size) | (alloc))
#define GET(p) (*(unsigned int *)(p))
#define PUT(p, val) (*(unsigned int *)(p) = (val))
#define GET_SIZE(p) (GET(p) & ~0x7)
#define GET_ALLOC(p) (GET(p) & 0x1)
#define PUT_PTR(p, ptr) (*(unsigned int *)(p) = (unsigned int)(ptr))
#define HDRP(bp) ((char *)(bp)-WSIZE)
#define FTRP(bp) ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)
#define NEXT_BLKP(p) ((char *)(p) + GET_SIZE((char *)(p)-WSIZE))
#define PREV_BLKP(p) ((char *)(p)-GET_SIZE((char *)(p)-DSIZE))
#define PRED_PTR(p) ((char *)(p))
#define SUCC_PTR(p) ((char *)(p) + WSIZE)
#define PRED(p) (*(char **)(p))
#define SUCC(p) (*(char **)(SUCC_PTR(p)))
#define SEG_LIST(p, idx) *((char **)p + idx)
#define GET_PREV(p) ((char *)(p))
#define GET_NEXT(p) ((char *)(p) + WSIZE)
#define GET_PREV_BLK(p) (*(char **)(p))
#define GET_NEXT_BLK(p) (*(char **)(GET_NEXT(p)))

char *heap_listp;
void *seg_listp;

static void *coalesce(void *bp);
static void *extend_heap(size_t words);
static void *seg_search(size_t adj_size);
static void *place(void *bp, size_t adj_size);
static void seg_insert(void *bp, size_t block_size);
static void seg_delete(void *bp);
int mm_check(void);

static void *extend_heap(size_t words) {
    char *bp;
    size_t size;

    size = (words % 2) ? (words + 1) * WSIZE : words * WSIZE;
    if ((long)(bp = mem_sbrk(size)) == -1) {
        return NULL;
    }

    PUT(HDRP(bp), PACK(size, 0));
    PUT(FTRP(bp), PACK(size, 0));
    PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1));
    seg_insert(bp, size);

    return coalesce(bp);
}

static void seg_insert(void *bp, size_t block_size) {
    void *list_ptr = NULL;
    void *insert_loc = NULL;
    int list_number = 0;

    while ((list_number < (SEG_LIST_MAX - 1)) && (block_size > 1)) {
        block_size = block_size >> 1;
        list_number++;
    }

    list_ptr = SEG_LIST(seg_listp, list_number);

    while ((list_ptr != NULL) && (block_size > GET_SIZE(HDRP(list_ptr)))) {
        insert_loc = list_ptr;
        list_ptr = GET_PREV_BLK(list_ptr);
    }

    if (list_ptr != NULL) {
        if (insert_loc != NULL) {
            PUT_PTR(GET_PREV(insert_loc), bp);
            PUT_PTR(GET_NEXT(bp), insert_loc);
            PUT_PTR(GET_PREV(bp), list_ptr);
            PUT_PTR(GET_NEXT(list_ptr), bp);
        } else {
            PUT_PTR(GET_NEXT(list_ptr), bp);
            PUT_PTR(GET_PREV(bp), list_ptr);
            PUT_PTR(GET_NEXT(bp), NULL);
            SEG_LIST(seg_listp, list_number) = bp;
        }
    } else {
        if (insert_loc != NULL) {
            PUT_PTR(GET_NEXT(bp), insert_loc);
            PUT_PTR(GET_PREV(insert_loc), bp);
            PUT_PTR(GET_PREV(bp), NULL);
        } else {
            SEG_LIST(seg_listp, list_number) = bp;
            PUT_PTR(GET_PREV(bp), NULL);
            PUT_PTR(GET_NEXT(bp), NULL);
            return;
        }
    }

    return;
}


static void seg_delete(void *bp) {
    int cnt = 0;
    size_t block_size = GET_SIZE(HDRP(bp));

    if (GET_NEXT_BLK(bp) == NULL) {
        while (cnt < (SEG_LIST_MAX - 1) && block_size > 1) {
            block_size = block_size >> 1;
            cnt++;
        }
        SEG_LIST(seg_listp, cnt) = GET_PREV_BLK(bp);
        if (SEG_LIST(seg_listp, cnt) != NULL) {
            PUT_PTR(GET_NEXT(SEG_LIST(seg_listp, cnt)), NULL);
        }
        return;
    }

    PUT_PTR(GET_PREV(GET_NEXT_BLK(bp)), GET_PREV_BLK(bp));
    if (GET_PREV_BLK(bp) != NULL) {
        PUT_PTR(GET_NEXT(GET_PREV_BLK(bp)), GET_NEXT_BLK(bp));
    }

    return;
}

static void *seg_search(size_t adj_size) {
    size_t size = adj_size;
    int list_number = 0;
    void *list_ptr = NULL;

    while (list_number < SEG_LIST_MAX) {
        if ((list_number == SEG_LIST_MAX - 1) || ((size <= 1) && (SEG_LIST(seg_listp, list_number) != NULL))) {
            list_ptr = SEG_LIST(seg_listp, list_number);

            while ((list_ptr != NULL) && (adj_size > GET_SIZE(HDRP(list_ptr)))) {
                list_ptr = GET_PREV_BLK(list_ptr);
            }
            if (list_ptr != NULL) {
                break;
            }
        }
        list_number++;
        size = size >> 1;
    }
    return list_ptr;
}

int mm_init(void) {
    int cnt;
    seg_listp = mem_sbrk(SEG_LIST_MAX * WSIZE);

    for (cnt = 0; cnt < SEG_LIST_MAX; cnt++) {
        SEG_LIST(seg_listp, cnt) = NULL;
    }

    if ((long)(heap_listp = mem_sbrk(4 * WSIZE)) == -1) {
        return -1;
    }

    PUT(heap_listp, 0);
    PUT(heap_listp + (1 * WSIZE), PACK(DSIZE, 1));
    PUT(heap_listp + (2 * WSIZE), PACK(DSIZE, 1));
    PUT(heap_listp + (3 * WSIZE), PACK(0, 1));
    heap_listp += (2 * WSIZE);

    if (extend_heap(INIT_CHUNKSIZE) == NULL) {
        return -1;
    }

    return 0;
}

void *mm_malloc(size_t size) {
    if (size == 0) {
        return NULL;
    }

    size_t adj_size;
    size_t extend_size;
    char *bp;
    char *ptr;

    if (size < DSIZE) {
        adj_size = 2 * DSIZE;
    } else {
        adj_size = ALIGN(size + DSIZE);
    }

    if ((bp = seg_search(adj_size)) != NULL) {
        ptr = place(bp, adj_size);
        return ptr;
    }

    extend_size = MAX(adj_size, CHUNKSIZE);
    if ((bp = extend_heap(extend_size / WSIZE)) == NULL) {
        return NULL;
    }

    ptr = place(bp, adj_size);

    return ptr;
}

static void *place(void *bp, size_t adj_size) {
    size_t block_size = GET_SIZE(HDRP(bp));
    void *next_ptr = NULL;
    seg_delete(bp);

    if ((block_size - adj_size) >= (2 * DSIZE)) {
        if ((block_size - adj_size) >= 250) {
            PUT(HDRP(bp), PACK(block_size - adj_size, 0));
            PUT(FTRP(bp), PACK(block_size - adj_size, 0));
            next_ptr = NEXT_BLKP(bp);
            PUT(HDRP(next_ptr), PACK(adj_size, 1));
            PUT(FTRP(next_ptr), PACK(adj_size, 1));
            seg_insert(bp, block_size - adj_size);
            return next_ptr;
        } else {
            PUT(HDRP(bp), PACK(adj_size, 1));
            PUT(FTRP(bp), PACK(adj_size, 1));
            next_ptr = NEXT_BLKP(bp);
            PUT(HDRP(next_ptr), PACK(block_size - adj_size, 0));
            PUT(FTRP(next_ptr), PACK(block_size - adj_size, 0));
            seg_insert(next_ptr, block_size - adj_size);
        }
    } else {
        PUT(HDRP(bp), PACK(block_size, 1));
        PUT(FTRP(bp), PACK(block_size, 1));
    }
    return bp;
}

static void *coalesce(void *bp) {
    size_t prev_alloc = GET_ALLOC(HDRP(PREV_BLKP(bp)));
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
    size_t size = GET_SIZE(HDRP(bp));

    if (prev_alloc && next_alloc) {
        return bp;
    }

    else if (prev_alloc && !next_alloc) {
        seg_delete(bp);
        seg_delete(NEXT_BLKP(bp));
        size += GET_SIZE(HDRP(NEXT_BLKP(bp)));
        PUT(HDRP(bp), PACK(size, 0));
        PUT(FTRP(bp), PACK(size, 0));
    }

    else if (!prev_alloc && next_alloc) {
        seg_delete(bp);
        seg_delete(PREV_BLKP(bp));
        size += GET_SIZE(HDRP(PREV_BLKP(bp)));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        PUT(FTRP(bp), PACK(size, 0));
        bp = PREV_BLKP(bp);
    } else {
        seg_delete(PREV_BLKP(bp));
        seg_delete(bp);
        seg_delete(NEXT_BLKP(bp));
        size += GET_SIZE(HDRP(PREV_BLKP(bp))) + GET_SIZE(FTRP(NEXT_BLKP(bp)));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        PUT(FTRP(NEXT_BLKP(bp)), PACK(size, 0));
        bp = PREV_BLKP(bp);
    }

    seg_insert(bp, size);
    return bp;
}


void mm_free(void *bp) {
    size_t size = GET_SIZE(HDRP(bp));

    PUT(HDRP(bp), PACK(size, 0));
    PUT(FTRP(bp), PACK(size, 0));
    seg_insert(bp, size);
    coalesce(bp);
}

void *mm_realloc(void *ptr, size_t size) {
    if (size == 0) {
        mm_free(ptr);
        return NULL;
    }

    if (ptr == NULL) {
        return mm_malloc(size);
    }

    void *input_ptr = ptr;
    void *realloc_ptr;
    void *remain_ptr;
    void *next_ptr;
    size_t original_size = GET_SIZE(HDRP(input_ptr)) - DSIZE;
    size_t realloc_size = ALIGN(size);
    size_t nextfree_size;

    if (realloc_size == original_size) {
        return input_ptr;
    } else if (realloc_size < original_size) {
        if (original_size - realloc_size <= 2 * DSIZE) {
            return input_ptr;
        }

        PUT(HDRP(input_ptr), PACK(realloc_size + DSIZE, 1));
        PUT(FTRP(input_ptr), PACK(realloc_size + DSIZE, 1));
        realloc_ptr = input_ptr;
        remain_ptr = NEXT_BLKP(realloc_ptr);

        PUT(HDRP(remain_ptr), PACK(original_size - realloc_size, 0));
        PUT(FTRP(remain_ptr), PACK(original_size - realloc_size, 0));
        seg_insert(remain_ptr, GET_SIZE(HDRP(remain_ptr)));
        coalesce(remain_ptr);
        return realloc_ptr;
    } else {
        next_ptr = NEXT_BLKP(input_ptr);

        if (next_ptr != NULL && (GET_ALLOC(HDRP(next_ptr)) == 0)) {
            nextfree_size = GET_SIZE(HDRP(next_ptr));
            if (nextfree_size + original_size >= realloc_size) {
                seg_delete(next_ptr);

                if (nextfree_size + original_size - realloc_size <= DSIZE) {
                    PUT(HDRP(input_ptr), PACK(original_size + nextfree_size, 1));
                    PUT(FTRP(input_ptr), PACK(original_size + nextfree_size, 1));
                    return input_ptr;
                } else {
                    PUT(HDRP(input_ptr), PACK(realloc_size + DSIZE, 1));
                    PUT(FTRP(input_ptr), PACK(realloc_size + DSIZE, 1));
                    realloc_ptr = input_ptr;
                    input_ptr = NEXT_BLKP(realloc_ptr);
                    PUT(HDRP(input_ptr), PACK(original_size + nextfree_size - realloc_size, 0));
                    PUT(FTRP(input_ptr), PACK(original_size + nextfree_size - realloc_size, 0));
                    seg_insert(input_ptr, GET_SIZE(HDRP(input_ptr)));
                    coalesce(input_ptr);
                    return realloc_ptr;
                }
            }
        }

        realloc_ptr = mm_malloc(size);
        memcpy(realloc_ptr, input_ptr, original_size);
        mm_free(input_ptr);

        return realloc_ptr;
    }
}
