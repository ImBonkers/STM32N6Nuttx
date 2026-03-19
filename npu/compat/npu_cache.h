#ifndef NPU_CACHE_H
#define NPU_CACHE_H

#include <stdint.h>

void npu_cache_enable_clocks_and_reset(void);
void npu_cache_disable_clocks_and_reset(void);
void npu_cache_enable(void);
void npu_cache_disable(void);
void npu_cache_invalidate(void);
void npu_cache_clean_invalidate_range(uint32_t start_addr, uint32_t end_addr);
void npu_cache_clean_range(uint32_t start_addr, uint32_t end_addr);

#endif
