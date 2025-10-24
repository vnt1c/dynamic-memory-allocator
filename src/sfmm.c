/**
 * Do not submit your assignment with a main function in this file.
 * If you submit with a main function in this file, you will get a zero.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>

#include "debug.h"
#include "sfmm.h"

/* ALL BASIC CONSTANTS */
#define WSIZE           8
#define DSIZE           16
#define MIN_BLOCK_SIZE  32

/* TEXTBOOK DEFINED MACROS WITH CHANGES */
#define GET(p)              (*(size_t *)(p))
#define PUT(p, val)         (*(size_t *)(p) = (val))

#define GET_SIZE(p)         (GET(p) & ~0xF)
#define IS_ALLOC(p)         ((GET(p) & 0x2) >> 1)
#define IS_PREV_ALLOC(p)    (GET(p) & 0x1)

// block is pointer to sf_block struct object
// these are different from textbook-defined macros
#define HDR_ADDR(block)     (&((block)->header))
#define FTR_ADDR(block)     ((char *)(block) + GET_SIZE(HDR_ADDR(block)))
#define PAYLOAD_ADDR(block) ((bp)->body.payload)

#define PACK(size, alloc, prev_alloc)       ((size) | ((alloc) << 1) | (prev_alloc))

/* Global Variables */
bool heap_is_init = false;

/* Function Prototypes */
void init_heap();
void expand_heap(size_t size);
sf_block *get_prev_block(sf_block *block);
sf_block *get_next_block(sf_block *block);
bool validate_pointer(sf_block *bp);
void set_header_footer(sf_block *block, size_t size, size_t alloc_flags);
size_t get_aligned_size(size_t size);
sf_block *get_fit_block(size_t size);
sf_block *coalesce(sf_block *block);
sf_block *split_block(sf_block *block, size_t size);
void insert_free_list(sf_block *block);
void remove_free_list(sf_block *block);

void *sf_malloc(size_t size) {
    // Spec: the first call to malloc should initialize the heap
    if (!heap_is_init) {
        init_heap();
    }

    if (size == 0) return NULL;

    size_t aligned_size = get_aligned_size(size);
    sf_block *block = get_fit_block(aligned_size);

    if (block == NULL) {
        expand_heap(aligned_size);
        block = get_fit_block(aligned_size);
        // No space left on the heap
        if (block == NULL) {
            sf_errno = ENOMEM;
            return NULL;
        }
        // remove_free_list(block);
    }

    // Split block, if necessary
    remove_free_list(block);
    block = split_block(block, aligned_size);

    // return the start of the struct + prev_footer (8B) + header (8B)
    return (void*) ((char*) block + DSIZE);
}
 
void sf_free(void *pp) {
    if (pp == NULL) abort();

    // Cast pp to sf_block* so that it is usable
    sf_block *bp = (sf_block*) ((char*)pp - DSIZE);

    // NULL case (cannot free null pointer)
    if (!validate_pointer(bp)) abort();

    /* Set header and footer
       Allocated bit should be cleared (we are freeing this block) */
    set_header_footer(bp, GET_SIZE(&bp->header), 0b00 | IS_PREV_ALLOC(&bp->header));
    // Update next block’s prev_alloc bit
    sf_block *next = get_next_block(bp);
    if (next != NULL)
        next->header &= ~0x1;

    bp = coalesce(bp); // Coalesce with potential nearby free blocks
    insert_free_list(bp); // Insert the new block into the appropriate free list
}

void *sf_realloc(void *pp, size_t rsize) {
    // Cast pp to sf_block* so that it is usable
    sf_block *bp = (sf_block*) ((char*)pp - DSIZE);
    size_t old_size = GET_SIZE(&bp->header);

    // NULL case (cannot free null pointer)
    if (!validate_pointer(bp)) abort();

    size_t new_size = get_aligned_size(rsize);
    // Case 0: pointer is valid and the rsize parameter is 0
    if (rsize == 0) {
        sf_free(pp);
        return NULL; // block no longer exists
    } 
    // Case 1: reallocating to the same size
    if (new_size == old_size) {
        return pp;
    }
    // Case 2: reallocating to a larger size
    else if (new_size > old_size) {
        void *new_pp = sf_malloc(rsize);
        if (new_pp == NULL) return NULL;

        memcpy(new_pp, pp, old_size - DSIZE);
        sf_free(pp);
        return new_pp;
    }
    // Case 3: reallocating to a smaller size
    if (new_size < old_size) {
        if ((old_size - new_size) >= MIN_BLOCK_SIZE) {
            // MANUAL SPLITTING (split_block doesn't work for this case)
            sf_block *block_after = (sf_block *)((char *)bp + new_size);

            // Set headers/footers of the current block and the block after
            set_header_footer(bp, new_size, 0b10 | IS_PREV_ALLOC(&bp->header));
            set_header_footer(block_after, old_size - new_size, 0b01); // old_size - new_size is the remaining bytes...

            block_after = coalesce(block_after);
            insert_free_list(block_after);
        }
        return pp;
    }
    return NULL;
}

/*
 * Initializes the heap.
 * Creates sentinel for each free list.
 */
void init_heap() {
    // Spec: We need to make one call to sf_mem_grow when the heap is empty to set up prologue/epilogue
    // Let's set up the heap first ...
    char *init = (char*) sf_mem_grow();
    if (init == NULL) return;

    // Spec: allocated blocks don't have footers, 8 bytes are for padding
    // prev_alloc already serves as 8-byte padding
    sf_block *prologue = (sf_block*) ((char*)sf_mem_start());
    prologue->header = PACK(MIN_BLOCK_SIZE,1,1); // prev_alloc = 1 since no prev block (treat as allocated...)

    // Spec: epilogue block size is 0
    sf_block *epilogue = (sf_block*)((char*) sf_mem_end() - DSIZE);
    epilogue->header = PACK(0,1,0);

    /* !!! TODO !!!     initialize first block (wilderness block) */
    sf_block *wilderness_block = (sf_block*)((char*) prologue + GET_SIZE(&prologue->header));
    // size is: page size - prologue - epilogue - padding
    size_t wilderness_size = PAGE_SZ - GET_SIZE(&prologue->header) - WSIZE;
    set_header_footer(wilderness_block, wilderness_size, 0b01);

    // Each list head is a sentinel
    for (int i = 0; i < NUM_FREE_LISTS; i++) {
        sf_free_list_heads[i].body.links.prev = &sf_free_list_heads[i];
        sf_free_list_heads[i].body.links.next = &sf_free_list_heads[i];
    }

    // Last free list ONLY contains wilderness block
    sf_free_list_heads[NUM_FREE_LISTS-1].body.links.prev = wilderness_block;
    sf_free_list_heads[NUM_FREE_LISTS-1].body.links.next = wilderness_block;

    wilderness_block->body.links.prev = &sf_free_list_heads[NUM_FREE_LISTS-1];
    wilderness_block->body.links.next = &sf_free_list_heads[NUM_FREE_LISTS-1];

    heap_is_init = true; // set init heap to true
}

void expand_heap(size_t size) {
    // Always have a wilderness block
    sf_block *wilderness = sf_free_list_heads[NUM_FREE_LISTS - 1].body.links.next;
    size_t wild_size = GET_SIZE(&wilderness->header);

    // Add wilderness size to needed size before computing pages
    size_t pages = (size - wild_size + 4095) / 4096;

    for (size_t i = 0; i < pages; i++) {
        // Grow the heap by one page
        char *new_page = (char*) sf_mem_grow();

        // Error check (page couldn't be allocated!)
        if (!new_page) {
            sf_errno = ENOMEM;
            return;
        }

        // Overwrite previous epilogue
        sf_block *tmp = (sf_block*) ((char*) new_page - DSIZE);
        // Set new page block's header and footer
        set_header_footer(tmp, PAGE_SZ, 0b00);

        // Create new epilogue
        sf_block *epilogue = (sf_block*)((char*) sf_mem_end() - DSIZE);
        epilogue->header = PACK(0,1,0);

        // show_heap("inside expand_heap");
        // Finally, coalesce the block with the previous wilderness block...
        tmp = coalesce(tmp);

        insert_free_list(tmp);
    }

    // Create new epilogue
    sf_block *epilogue = (sf_block*)((char*) sf_mem_end() - DSIZE);
    epilogue->header = PACK(0,1,0);
}

sf_block *get_prev_block(sf_block *block) {
    // Confirm the previous block is free before continuing
    if (IS_PREV_ALLOC(&block->header)) return NULL;
    
    sf_block *prev_block = (sf_block*) ((char*) block - GET_SIZE(&block->prev_footer));
    // Heap out of bounds
    // fprintf(stderr, "prev_block: %p, after prologue: %p\n", prev_block,(char*) sf_mem_start() + 32 );
    if ((char*) prev_block < (char*) sf_mem_start() + 32) return NULL;

    return prev_block;
}

sf_block *get_next_block(sf_block *block) {
    sf_block *next_block = (sf_block*) ((char*) block + GET_SIZE(&block->header));
    if ((char*) next_block >= (char*) sf_mem_end() - 8) return NULL;
    // fprintf(stderr, "next block addr: %p\n", next_block);

    return next_block;
}

/**
 * @param p some pointer
 * 
 * @return false if the pointer is not valid and true if the pointer is valid
 */
bool validate_pointer(sf_block *bp) {
    // Pointer is NULL
    if (bp == NULL) return false;

    // Not 16-byte aligned
    if (((uintptr_t)&bp->body) % 16 != 0) return false;

    // Block size is less than MIN_BLOCK_SIZE
    if (GET_SIZE(&bp->header) < MIN_BLOCK_SIZE) return false;

    // Block size is not a multiple of 16
    if (GET_SIZE(&bp->header) % 16 != 0) return false;

    // Header is before the start of the heap + prologue + padding
    if ((char*) HDR_ADDR(bp) < (char*) sf_mem_start() + MIN_BLOCK_SIZE + 8) return false;

    // Footer is after the end of the heap
    if ((char*) FTR_ADDR(bp) > (char*) sf_mem_end() - 8) return false;

    // Block is not allocated
    if (!IS_ALLOC(&bp->header)) return false;

    // prev_alloc is not set for bp but alloc is set for the previous block's header
    if (!IS_PREV_ALLOC(&bp->header)) {
        sf_block *prev = get_prev_block(bp);
        if (prev == NULL || IS_ALLOC(&prev->header)) return false;
    }

    return true;
}

void set_header_footer(sf_block *block, size_t size, size_t alloc_flags) {
    size_t header_footer = size | alloc_flags;

    // Set all values
    PUT(HDR_ADDR(block), header_footer);
    if ((alloc_flags & 0x2) == 0) {
        PUT(FTR_ADDR(block), header_footer);
    }
}

size_t get_aligned_size(size_t size) {
    // size + header
    size_t adjusted_size = size + WSIZE;

    size_t remainder = adjusted_size % 16;
    if (remainder != 0)
        adjusted_size += (16 - remainder);

    if (adjusted_size < MIN_BLOCK_SIZE) {
        return MIN_BLOCK_SIZE;
    }

    return adjusted_size;
}

/**
 * @param size - the size of the block to search for
 * 
 * @return the sf_block to store the allocated memory
 */
sf_block *get_fit_block(size_t size) {
    /* FIND SMALLEST APPROPRIATE SIZE CLASS */

    // Size too small
    if (size < MIN_BLOCK_SIZE) return NULL;

    int index = 0;
    int low = MIN_BLOCK_SIZE, high = MIN_BLOCK_SIZE * 2;
    // Find the size class of block
    // Spec: Geometric sequence based (wilderness block is the "all other" case)
    for (index = 1; index < NUM_FREE_LISTS-2; index++) {
        if (low < size && size <= high) break;
        low *= 2;
        high *= 2;
    }
    // fprintf(stderr, "get_fit_block: index is %d\n", index);

    // Edge case: sf_free_list_heads[0]
    if (size == MIN_BLOCK_SIZE) index = 0;
    // Edge case: overflow
    if (index >= NUM_FREE_LISTS - 1) index = NUM_FREE_LISTS - 2;

    /* FIND FIRST FREE BLOCK */
    for (; index < NUM_FREE_LISTS; index++) {
        sf_block *head = &sf_free_list_heads[index];
        sf_block *curr = head->body.links.next;

        while (curr != head) {
            if (GET_SIZE(&curr->header) >= size) {
                return curr;
            }
            // advance cursor
            curr = curr->body.links.next;
        }
    }
    
    // No free block of appropriate size
    return NULL;
}

sf_block *coalesce(sf_block *block) { 
    if (block == NULL) return NULL;
    // left | block | right
    // Get previous and next blocks...
    sf_block *left = get_prev_block(block);
    sf_block *right = get_next_block(block);
    // fprintf(stderr, "LEFT IS NULL: %d, RIGHT IS NULL: %d\n", (left == NULL), (right == NULL));

    /* Coalesce with immediate previous and next neighbors, if they are free */
    // prev_block is free
    if (left != NULL && !IS_ALLOC(&left->header)) {
        remove_free_list(left);
        // current is already free (0)
        size_t flags = IS_PREV_ALLOC(&left->header);
        size_t size = GET_SIZE(&left->header) + GET_SIZE(&block->header);

        set_header_footer(left, size, flags);
        block = left;
    }
    // next_block is free
    if (right != NULL && !IS_ALLOC(&right->header)) {
        remove_free_list(right);
        // current is already free (0)
        size_t flags = IS_PREV_ALLOC(&block->header);
        size_t size = GET_SIZE(&block->header) + GET_SIZE(&right->header);

        set_header_footer(block, size, flags);
    }
    return block;
}   

sf_block *split_block(sf_block *block, size_t size) {
    size_t total_size = GET_SIZE(&block->header);
    size_t next_size = total_size - size;

    if (next_size < MIN_BLOCK_SIZE) {
        size_t flags = 0b10 | IS_PREV_ALLOC(&block->header);
        set_header_footer(block, total_size, flags);
        return block;
    }

    size_t flags = 0b10 | IS_PREV_ALLOC(&block->header);
    set_header_footer(block, size, flags);
    
    sf_block *new_block = get_next_block(block);
    set_header_footer(new_block, next_size, 0b01);
    insert_free_list(new_block);

    return block;
}

/**
 * @param block - the free block to be inserted into the free list
 * 
 * Inserts a free block into the free list in the appropriate size class.
 * !!! Note: this function only works for free lists 0-9 (NOT wilderness block)
 */
void insert_free_list(sf_block *block) {
    size_t block_size = GET_SIZE(&block->header);
    int index = 0;
    int low = MIN_BLOCK_SIZE, high = MIN_BLOCK_SIZE * 2;
    // Find the size class of block
    // Spec: Geometric sequence based (wilderness block is the "all other" case)
    for (index = 1; index < NUM_FREE_LISTS-2; index++) {
        if (low < block_size && block_size <= high) {
            break;
        }
        low *= 2;
        high *= 2;
    }

    // Edge case: sf_free_list_heads[0]
    if (block_size == MIN_BLOCK_SIZE) index = 0;
    // Edge case: overflow
    if (index >= NUM_FREE_LISTS - 1) index = NUM_FREE_LISTS - 2;

    block->body.links.next = sf_free_list_heads[index].body.links.next;
    block->body.links.prev = &sf_free_list_heads[index];
    sf_free_list_heads[index].body.links.next->body.links.prev = block;
    sf_free_list_heads[index].body.links.next = block;
    // fprintf(stderr, "INSERTED: free list index %d\n", index);
}

/**
 * @param block - block to be removed
 * 
 * Removes a specified block from the free lists
 */
void remove_free_list(sf_block *block) {
    // a <-> b <-> c   ===>   a <-> c
    block->body.links.next->body.links.prev = block->body.links.prev;
    block->body.links.prev->body.links.next = block->body.links.next;
    block->body.links.next = NULL, block->body.links.prev = NULL; // removing links
}