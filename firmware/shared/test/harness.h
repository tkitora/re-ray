// 依存なしの最小テストハーネス。
#pragma once
#include <stdio.h>

extern int g_fail;
extern int g_checks;

#define CHECK(cond)                                           \
  do {                                                        \
    g_checks++;                                               \
    if (!(cond)) {                                            \
      g_fail++;                                               \
      printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
    }                                                         \
  } while (0)

void run_core_tests();
void run_gateway_tests();
void run_uart_tests();
