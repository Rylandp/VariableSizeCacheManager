#include "cachelab.h"
#include <stdio.h>
#include <stdlib.h>

/* ---------- Provided pointer and alignment helpers ---------- */

static inline size_t round_up(size_t val, size_t align) {
    return (val + align - 1) & ~(align - 1);
}

static inline block_header_t *header_from_payload(void *payload) {
    return (block_header_t *)((unsigned char *)payload - HEADER_SIZE);
}

static inline void *payload_from_header(block_header_t *h) {
    return (void *)((unsigned char *)h + HEADER_SIZE);
}

#define UNFINISHED(name) do { \
    fprintf(stderr, "TODO pool.c: implement %s\n", name); \
    exit(EXIT_FAILURE); \
} while (0)

/* ---------- Provided pool initialization ---------- */

int pool_init(memory_pool_t *pool, size_t size, alloc_policy_t policy) {
    if (size < MIN_BLOCK_SIZE)
        return -1;

    pool->bytes = (unsigned char *)malloc(size);
    if (!pool->bytes)
        return -1;

    block_header_t *first = (block_header_t *)pool->bytes;
    first->size = size - HEADER_SIZE;
    first->free = 1;
    first->next = NULL;
    pool->blocks = first;
    pool->size = size;
    pool->policy = policy;
    return 0;
}

/* ---------- Student work: search the embedded block list ---------- */

static block_header_t *first_fit(memory_pool_t *pool, size_t requested) {
    block_header_t *cur = pool->blocks;
    while (cur != NULL) {
        if(cur->free && cur->size >= requested) {
            return cur;
        }
        cur = cur->next;
    }
    return NULL;
}

static block_header_t *best_fit(memory_pool_t *pool, size_t requested) {
    block_header_t *cur = pool->blocks;
    block_header_t *best = NULL;
    while(cur != NULL) {
        if(cur->free && cur->size >= requested && (best == NULL || cur->size < best->size)){
            best = cur;
        }
        cur = cur->next;
    }
    return best;
}

/* ---------- Student work: carve a second header inside the pool ---------- */

static void split_if_possible(block_header_t *block, size_t requested) {
    size_t aligned = round_up(requested, HEADER_ALIGNMENT);
    if(block->size < aligned){
        return;
    }
    size_t remainder = block->size - aligned;
    if(remainder < MIN_BLOCK_SIZE){
        return;
    }  
    block_header_t *newBlock = (block_header_t *)((unsigned char *)payload_from_header(block) + aligned);
    newBlock->size = remainder-HEADER_SIZE;
    newBlock->free = 1;
    newBlock->next = block->next;

    block->size = aligned;
    block->next = newBlock;
}

/* ---------- Student work: select, split and occupy ---------- */

void *pool_try_alloc(memory_pool_t *pool, size_t requested) {
    /* TODO: Dispatch to first_fit or best_fit according to pool->policy.
     * Return NULL without changing the list when no block fits. Otherwise
     * split if possible, mark the selected block allocated and return its
     * payload. Do not evict here: the cache handler owns that retry loop.
     * The caller already holds the provided cache/output locks. */
    block_header_t *ret = NULL;
    if(pool->policy == ALLOC_FIRST_FIT) {
        ret = first_fit(pool, requested);
    } else {
        ret = best_fit(pool, requested);
    }
    if(ret == NULL) {
        return NULL;
    }
    split_if_possible(ret, requested);
    ret->free = 0;
    return payload_from_header(ret);
}

/* ---------- Student work: free and coalesce ---------- */

void pool_free(memory_pool_t *pool, void *payload, event_context_t *ev) {
    /* TODO: NULL is a no-op. Recover the header, mark it free, absorb a
     * free successor if present, then walk from pool->blocks to find and
     * absorb into a free predecessor if present. A merge adds the removed
     * header's bytes as well as its payload to the surviving block; repair
     * next links. Emit one COALESCE per actual merge via output_event(ev,...)
     * with req/client IDs, surviving payload offset and new payload size.
     * No mutex operations belong here; the worker already holds the locks. */
    if (payload == NULL) {
        return;
    }
    block_header_t *block = header_from_payload(payload);
    block->free = 1;

    if(block->next != NULL && block->next->free) {    
        block_header_t *successor = block->next;
        block->size += HEADER_SIZE + successor->size;
        block->next = successor->next;
        output_event(ev, 
                    "COALESCE req=%zu client=%d addr=%zu size=%zu",
                    ev->req_seq,
                    ev->client_id,
                    pool_offset(pool, payload_from_header(block)),
                    block->size);
    }

    block_header_t *prev = NULL;
    block_header_t *cur = pool->blocks;

    while(cur != NULL && cur != block) {
        prev = cur;
        cur = cur->next;
    }

    if (prev != NULL && prev->free) {
        prev->size += HEADER_SIZE + block->size;
        prev->next = block->next;
        output_event(ev, 
                    "COALESCE req=%zu client=%d addr=%zu size=%zu",
                    ev->req_seq,
                    ev->client_id,
                    pool_offset(pool, payload_from_header(prev)),
                    prev->size);
    }
}

/* ---------- Provided inspection and teardown ---------- */

size_t pool_offset(memory_pool_t *pool, void *payload) {
    return (size_t)((unsigned char *)payload - pool->bytes);
}

size_t pool_payload_capacity(void *payload) {
    block_header_t *h = header_from_payload(payload);
    return h->size;
}

void pool_inspect(memory_pool_t *pool, size_t *free_payload,
                  size_t *free_blocks, size_t *largest_free) {
    *free_payload = 0;
    *free_blocks = 0;
    *largest_free = 0;
    block_header_t *cur = pool->blocks;
    while (cur) {
        if (cur->free) {
            (*free_blocks)++;
            *free_payload += cur->size;
            if (cur->size > *largest_free)
                *largest_free = cur->size;
        }
        cur = cur->next;
    }
}

void print_pool(memory_pool_t *pool) {
    fprintf(stderr, "Pool %p size=%zu\n", (void *)pool->bytes, pool->size);
    block_header_t *cur = pool->blocks;
    while (cur) {
        fprintf(stderr, "  block=%p size=%zu free=%d next=%p\n",
                (void *)cur, cur->size, cur->free, (void *)cur->next);
        cur = cur->next;
    }
}

void pool_destroy(memory_pool_t *pool) {
    free(pool->bytes);
    pool->bytes = NULL;
    pool->blocks = NULL;
}
