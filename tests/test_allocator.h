/** @file test_allocator.h @brief 仅测试目标使用的分配故障注入。 */
#ifndef DISPLAY_TEST_ALLOCATOR_H
#define DISPLAY_TEST_ALLOCATOR_H
#include <stddef.h>
extern int test_alloc_fail;
extern unsigned test_alloc_live, test_alloc_calls;
void *test_calloc(size_t count, size_t size);
void test_free(void *ptr);
#endif
