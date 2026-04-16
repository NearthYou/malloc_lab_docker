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
team_t team = {
    /* Team name */
    "ateam",
    /* First member's full name */
    "Harry Bovik",
    /* First member's email address */
    "bovik@cs.cmu.edu",
    /* Second member's full name (leave blank if none) */
    "",
    /* Second member's email address (leave blank if none) */
    ""};

/* single word (4) or double word (8) alignment */
#define ALIGNMENT 8
#define WSIZE 4
#define DSIZE 8
#define CHUNKSIZE (1<<12)
#define PTRSIZE sizeof(void *)
#define MINBLOCKSIZE (2 * DSIZE + 2 * PTRSIZE)

/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(size) (((size) + (ALIGNMENT - 1)) & ~0x7)

#define SIZE_T_SIZE (ALIGN(sizeof(size_t)))
#define MAX(x, y) ((x) > (y) ? (x) : (y))
#define PACK(size, alloc) ((size) | (alloc))

#define GET(p) (*(unsigned int *)(p))
#define PUT(p, val) (*(unsigned int *)(p) = (val))

#define GET_SIZE(p) (GET(p) & ~0x7)
#define GET_ALLOC(p) (GET(p) & 0x1)

#define HDRP(bp) ((char *)(bp) - WSIZE)
#define FTRP(bp) ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)

#define NEXT_BLKP(bp) ((char *)(bp) + GET_SIZE(((char *)(bp) - WSIZE)))
#define PREV_BLKP(bp) ((char *)(bp) - GET_SIZE(((char *)(bp) - DSIZE)))

#define GET_PRED_FREEP(bp) (*(char **)(bp))
/*
 * Free block payload stores two pointer-sized fields.
 * On 64-bit builds, using WSIZE (4) here corrupts the list by overlapping
 * pred and succ, which leads to invalid pointer dereferences.
 */
#define GET_SUCC_FREEP(bp) (*(char **)((char *)(bp) + PTRSIZE))

#define PUT_PRED_FREEP(bp, ptr) (*(char **)(bp) = (ptr))
#define PUT_SUCC_FREEP(bp, ptr) (*(char **)((char *)(bp) + PTRSIZE) = (ptr))

static char *heap_listp;
static char *explict_listp;
static char *last_listp;

static void *extend_heap(size_t words);
static void *coalesce(void *bp);
static void *first_fit(size_t asize);
static void *next_fit(size_t asize);
static void *best_fit(size_t asize);
static void place(void *bp, size_t asize);
static void add_free_list(void *bp);
static void remove_free_list(void *bp);

/*
 * mm_init - initialize the malloc package.
 */
int mm_init(void)
{
    if ((heap_listp = mem_sbrk(6 * WSIZE)) == (void *)-1)
        return -1;

    PUT(heap_listp, 0);
    PUT(heap_listp + (1 * WSIZE), PACK(2 * DSIZE, 1));
    PUT(heap_listp + (2 * WSIZE), 0);
    PUT(heap_listp + (3 * WSIZE), 0);
    PUT(heap_listp + (4 * WSIZE), PACK(2 * DSIZE, 1));
    PUT(heap_listp + (5 * WSIZE), PACK(0, 1));

    explict_listp = NULL; 

    heap_listp += (2 * WSIZE);

    if (extend_heap(CHUNKSIZE / WSIZE) == NULL)
        return -1;

    return 0;
}

static void *extend_heap(size_t words)
{
    char *bp;
    size_t size;

    size = (words % 2) ? (words + 1) * WSIZE : words * WSIZE;
    if ((long)(bp = mem_sbrk(size)) == -1)
        return NULL;
    
    PUT(HDRP(bp), PACK(size, 0));
    PUT(FTRP(bp), PACK(size, 0));
    PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1));

    return coalesce(bp);
}

/*
 * mm_malloc - Allocate a block by incrementing the brk pointer.
 *     Always allocate a block whose size is a multiple of the alignment.
 */
void *mm_malloc(size_t size)
{
    size_t asize;      /* 조정된 블록 크기 */
    size_t extendsize; /* 힙 확장량 (fit이 없을 경우) */
    char *bp;

    if (size == 0)
        return NULL;

    /*
     * Explicit List에서 Free block은 PRED와 SUCC 포인터(각 8바이트)를 담아야 함.
     * 헤더(4) + 푸터(4) + 포인터(16) = 총 24바이트가 최소 크기.
     * 따라서 size에 헤더+푸터(DSIZE)를 더한 값이 24보다 작으면 24로 고정.
     */
    if (size <= MINBLOCKSIZE - DSIZE)
        asize = MINBLOCKSIZE;
    else
        asize = DSIZE * ((size + (DSIZE) + (DSIZE - 1)) / DSIZE);

    if ((bp = first_fit(asize)) != NULL)
    {
        place(bp, asize);
        return bp;
    }

    extendsize = MAX(asize, CHUNKSIZE);
    if ((bp = extend_heap(extendsize / WSIZE)) == NULL)
        return NULL;
    
    place(bp, asize);
    return bp;
}

static void *coalesce(void *bp)
{
    size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(bp)));
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
    size_t size = GET_SIZE(HDRP(bp));

    if (prev_alloc && next_alloc)
    {
        add_free_list(bp);
        return bp;
    }
    else if (prev_alloc && !next_alloc)
    {
        remove_free_list(NEXT_BLKP(bp));
        size += GET_SIZE(HDRP(NEXT_BLKP(bp)));
        PUT(HDRP(bp), PACK(size, 0));
        PUT(FTRP(bp), PACK(size, 0));
    }
    else if (!prev_alloc && next_alloc)
    {
        remove_free_list(PREV_BLKP(bp));
        size += GET_SIZE(HDRP(PREV_BLKP(bp)));
        PUT(FTRP(bp), PACK(size, 0));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        bp = PREV_BLKP(bp);
    }
    else
    {
        remove_free_list(PREV_BLKP(bp));
        remove_free_list(NEXT_BLKP(bp));
        size += GET_SIZE(HDRP(PREV_BLKP(bp))) + GET_SIZE(FTRP(NEXT_BLKP(bp)));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        PUT(FTRP(NEXT_BLKP(bp)), PACK(size, 0));
        bp = PREV_BLKP(bp);
    }
    add_free_list(bp);
    return bp;
}

// free list에서 들어갈 수 있는 block을 찾는다.
// 전체 힙을 다 확인
static void *first_fit(size_t asize)
{
    // FirstFit
    // heap_listp 를 순회 해서 free block을 발견하면
    // 크기 비교해서 작거나 같다 그럼 
    // 해당 bp를 반환

    for(char *bp = explict_listp; bp != NULL; bp = GET_SUCC_FREEP(bp))
    {
        if(!GET_ALLOC(HDRP(bp)) && (asize <= GET_SIZE(HDRP(bp))))
        {
            return bp;
        }
    }
    
    return NULL;
}

static void *next_fit(size_t asize)
{
    char *bp;

    if (last_listp == NULL)
        last_listp = explict_listp;

    for (bp = last_listp; GET_SIZE(HDRP(bp)) != 0; bp = NEXT_BLKP(bp))
    {
        if (!GET_ALLOC(HDRP(bp)) && (asize <= GET_SIZE(HDRP(bp))))
            return bp;
    }

    for (bp = explict_listp; bp < last_listp; bp = NEXT_BLKP(bp))
    {
        if (!GET_ALLOC(HDRP(bp)) && (asize <= GET_SIZE(HDRP(bp))))
            return bp;
    }
    
    return NULL;
}

static void *best_fit(size_t asize)
{
    char *best = NULL;
    size_t min_size = (size_t) -1;
    for (char *bp = explict_listp; GET_SIZE(HDRP(bp)) > 0; bp = NEXT_BLKP(bp))
    {
        if (!GET_ALLOC(HDRP(bp)) && (asize <= GET_SIZE(HDRP(bp))))
        {
           // GET_SIZE(HDRP(bp)) - asize 가 최소인 경우 
           // 해당 bp 저장
           size_t sub = GET_SIZE(HDRP(bp)) - asize;
           if (sub < min_size)
           {
                min_size = sub;
                best = bp;

                if(sub == 0)
                    break;
           }
        }
    }

    return best;
}

// 찾은 free block 안에 allocated block을 배치하고, 남는 부분이 충분히 크면 split
static void place(void *bp, size_t asize)
{
    remove_free_list(bp);
    size_t size = GET_SIZE(HDRP(bp));
    /*
     * Any split remainder must be large enough to hold header/footer and
     * the explicit free-list pred/succ pointers.
     */
    if((size - asize) >= MINBLOCKSIZE)
    {
        PUT(HDRP(bp), PACK(asize, 1));
        PUT(FTRP(bp), PACK(asize, 1));
        bp = NEXT_BLKP(bp);
        PUT(HDRP(bp), PACK(size - asize, 0));
        PUT(FTRP(bp), PACK(size - asize, 0));
        add_free_list(bp);
    }
    else
    {
        PUT(HDRP(bp), PACK(size, 1));
        PUT(FTRP(bp), PACK(size, 1));
    }
}

/*
 * mm_free - Freeing a block does nothing.
 */
void mm_free(void *ptr)
{
    size_t size = GET_SIZE(HDRP(ptr));

    PUT(HDRP(ptr), PACK(size, 0));
    PUT(FTRP(ptr), PACK(size, 0));
    coalesce(ptr);
}

/*
 * mm_realloc - Implemented simply in terms of mm_malloc and mm_free
 */
void *mm_realloc(void *ptr, size_t size)
{
    if (ptr == NULL)
        return mm_malloc(size);
    
    if (size == 0)
    {
        mm_free(ptr);
        return NULL;
    }

    /* realloc also has to preserve enough room for a future free-list node. */
    size_t new_size = (size <= MINBLOCKSIZE - DSIZE) ? MINBLOCKSIZE : DSIZE * ((size + (DSIZE) + (DSIZE - 1)) / DSIZE);
    size_t old_size = GET_SIZE(HDRP(ptr));

    if (new_size <= old_size)
    {
        return ptr;
    }

    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(ptr)));
    size_t next_size = GET_SIZE(HDRP(NEXT_BLKP(ptr)));
    size_t prev_alloc = GET_ALLOC(HDRP(PREV_BLKP(ptr)));
    size_t prev_size = GET_SIZE(HDRP(PREV_BLKP(ptr)));

    size_t next_total = old_size + next_size;
    if (!next_alloc && next_total >= new_size)
    {
        remove_free_list(NEXT_BLKP(ptr));
        if ((next_total - new_size) >= MINBLOCKSIZE)
        {
            PUT(HDRP(ptr), PACK(new_size, 1));
            PUT(FTRP(ptr), PACK(new_size, 1));
            void *rem_b = NEXT_BLKP(ptr);
            PUT(HDRP(rem_b), PACK(next_total - new_size, 0));
            PUT(FTRP(rem_b), PACK(next_total - new_size, 0));
            add_free_list(rem_b);
        }
        else
        {
            PUT(HDRP(ptr), PACK(next_total, 1));
            PUT(FTRP(ptr), PACK(next_total, 1));
        }
        return ptr;
    }

    size_t combined_total = old_size + prev_size;
    if (!prev_alloc)
    {
        if (!next_alloc)
        {
            combined_total += next_size;
            remove_free_list(NEXT_BLKP(ptr));
        }

        if (combined_total >= new_size)
        {
            void *prev_b = PREV_BLKP(ptr);

            /*
             * prev_b is currently a free block, so its payload begins with
             * explicit-list pointers. Unlink it before memmove overwrites
             * those fields, or remove_free_list will read corrupted pointers.
             */
            remove_free_list(prev_b);
            memmove(prev_b, ptr, old_size - DSIZE);

            if ((combined_total - new_size) >= MINBLOCKSIZE)
            {
                PUT(HDRP(prev_b), PACK(new_size, 1));
                PUT(FTRP(prev_b), PACK(new_size, 1));

                void *rem_b = NEXT_BLKP(prev_b);
                PUT(HDRP(rem_b), PACK(combined_total - new_size, 0));
                PUT(FTRP(rem_b), PACK(combined_total - new_size, 0));
                add_free_list(rem_b);
            }
            else
            {

                PUT(HDRP(prev_b), PACK(combined_total, 1));
                PUT(FTRP(prev_b), PACK(combined_total, 1));
            }
            return prev_b;
        }
    }

    void *new_ptr = mm_malloc(size);
    if (new_ptr == NULL)
        return NULL;
    memcpy(new_ptr, ptr, old_size - DSIZE);
    mm_free(ptr);
    return new_ptr;
}

static void add_free_list(void *bp)
{
    PUT_SUCC_FREEP(bp, explict_listp);
    PUT_PRED_FREEP(bp, NULL);
    if (explict_listp != NULL)
    {
        PUT_PRED_FREEP(explict_listp, bp);
    }
    explict_listp = bp;
}

static void remove_free_list(void *bp)
{
    if (bp == NULL || explict_listp == NULL)
        return;

    void *pred = GET_PRED_FREEP(bp);
    void *succ = GET_SUCC_FREEP(bp);

    if (bp == explict_listp)
    {
        explict_listp = succ;
    }
    else if (pred != NULL)
    {
        PUT_SUCC_FREEP(pred, succ);
    }

    if (succ != NULL) {
        PUT_PRED_FREEP(succ, pred);
    }
    
    PUT_PRED_FREEP(bp, NULL);
    PUT_SUCC_FREEP(bp, NULL);
}
