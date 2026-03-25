/**
 * MCU D-cache maintenance for LL_ATON on NuttX.
 * Replaces ST's mcu_cache.c which depends on CMSIS SCB_* functions.
 *
 * IMPORTANT: On Cortex-M55 Secure state, MVA-based cache ops
 * (DCIMVAC, DCCMVAC, DCCIMVAC) fail silently (pitfall #32).
 * ALL range operations use set/way (full D-cache) operations
 * instead.  This is less efficient but correct.
 */

#include <nuttx/config.h>
#include <stdbool.h>
#include "mcu_cache.h"
#ifdef CONFIG_ARMV8M_DCACHE
#include <nuttx/cache.h>
#endif

bool mcu_cache_is_enabled(void)
{
#ifdef CONFIG_ARMV8M_DCACHE
  return (*(volatile uint32_t *)0xE000ED14 & (1 << 16)) != 0;
#else
  return false;
#endif
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
  (void)start_addr;
  (void)end_addr;
  if (mcu_cache_is_enabled())
    {
      /* MVA ops (DCIMVAC) broken on M55 Secure — use set/way */

      up_flush_dcache_all();
    }
}

void mcu_cache_clean_range(uint32_t start_addr, uint32_t end_addr)
{
  (void)start_addr;
  (void)end_addr;
  if (mcu_cache_is_enabled())
    {
      /* MVA ops (DCCMVAC) broken on M55 Secure — use set/way */

      up_flush_dcache_all();
    }
}

void mcu_cache_clean_invalidate_range(uint32_t start_addr, uint32_t end_addr)
{
  (void)start_addr;
  (void)end_addr;
  if (mcu_cache_is_enabled())
    {
      /* MVA ops (DCCIMVAC) broken on M55 Secure — use set/way */

      up_flush_dcache_all();
    }
}
