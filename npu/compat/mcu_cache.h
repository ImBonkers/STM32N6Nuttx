#ifndef __MCU_CACHE_H
#define __MCU_CACHE_H

#include <stdint.h>

void mcu_cache_invalidate(void);
void mcu_cache_clean(void);
void mcu_cache_clean_invalidate(void);
void mcu_cache_invalidate_range(uint32_t start_addr, uint32_t end_addr);
void mcu_cache_clean_range(uint32_t start_addr, uint32_t end_addr);
void mcu_cache_clean_invalidate_range(uint32_t start_addr, uint32_t end_addr);

#endif
