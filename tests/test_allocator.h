/**
 * @file    test_allocator.h
 * @brief   提供测试专用分配故障注入接口。
 */
#ifndef DISPLAY_TEST_ALLOCATOR_H
#define DISPLAY_TEST_ALLOCATOR_H
#include <stddef.h>
extern int test_alloc_fail;
extern unsigned test_alloc_live, test_alloc_calls;

/**
 * @brief 测试专用 calloc 包装，支持分配故障注入。
 *
 * @param count 元素数
 * @param size 单元素字节数
 * @return 分配地址；注入失败或分配失败时为 NULL。
 */
void *test_calloc(size_t count, size_t size);

/**
 * @brief 释放测试分配并更新存活计数。
 *
 * @param ptr 待释放地址；可为 NULL
 */
void test_free(void *ptr);
#endif
