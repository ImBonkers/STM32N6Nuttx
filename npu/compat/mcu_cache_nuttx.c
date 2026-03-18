/**
 * MCU D-cache maintenance for LL_ATON on NuttX.
 * Replaces ST's mcu_cache.c which depends on CMSIS SCB_* functions.
 * Uses NuttX cache API (up_invalidate_dcache, etc.) instead.
 */

#include <stdbool.h>
#include "mcu_cache.h"
#include <nuttx/cache.h>

bool mcu_cache_is_enabled(void)
{
  /* Check CCR.DC bit */

  return (*(volatile uint32_t *)0xE000ED14 & (1 << 16)) != 0;
}

void mcu_cache_invalidate(void)
{
  if (mcu_cache_is_enabled())
    {
      up_invalidate_dcache_all();
    }
}

void mcu_cache_clean(void)
{
  if (mcu_cache_is_enabled())
    {
      up_clean_dcache_all();
    }
}

void mcu_cache_clean_invalidate(void)
{
  if (mcu_cache_is_enabled())
    {
      up_flush_dcache_all();
    }
}

void mcu_cache_invalidate_range(uint32_t start_addr, uint32_t end_addr)
{
  if (mcu_cache_is_enabled())
    {
      up_invalidate_dcache(start_addr, end_addr);
    }
}

void mcu_cache_clean_range(uint32_t start_addr, uint32_t end_addr)
{
  if (mcu_cache_is_enabled())
    {
      up_clean_dcache(start_addr, end_addr);
    }
}

void mcu_cache_clean_invalidate_range(uint32_t start_addr, uint32_t end_addr)
{
  if (mcu_cache_is_enabled())
    {
      up_flush_dcache(start_addr, end_addr);
    }
}
