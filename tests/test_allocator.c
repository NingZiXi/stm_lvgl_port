#include "test_allocator.h"
#include <assert.h>
#include <stdlib.h>
int test_alloc_fail;
unsigned test_alloc_live, test_alloc_calls;
static void *allocated[16];

void *test_calloc(size_t count, size_t size)
{
    ++test_alloc_calls;
    if (test_alloc_fail)
        return NULL;
    void *ptr = calloc(count, size);
    if (!ptr)
        return NULL;
    for (unsigned i = 0; i < 16; ++i)
        if (!allocated[i])
        {
            allocated[i] = ptr;
            ++test_alloc_live;
            return ptr;
        }
    assert(0 && "test allocation table full");
    return NULL;
}

void test_free(void *ptr)
{
    if (!ptr)
        return;
    for (unsigned i = 0; i < 16; ++i)
        if (allocated[i] == ptr)
        {
            allocated[i] = NULL;
            --test_alloc_live;
            free(ptr);
            return;
        }
    assert(0 && "invalid or double free");
}
