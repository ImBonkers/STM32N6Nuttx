/**
 * NPU CACHEAXI maintenance for LL_ATON on NuttX.
 * Replaces ST's npu_cache.c which depends on HAL_CACHEAXI_* functions.
 * Uses direct register access matching our stm32_bringup.c implementation.
 */

#include "npu_cache.h"
#include <stdint.h>
#include <stdbool.h>

/* CACHEAXI register offsets (base = 0x580DFC00) */

#define CACHEAXI_BASE     0x580DFC00UL
#define CACHEAXI_CR1      (*(volatile uint32_t *)(CACHEAXI_BASE + 0x00))
#define CACHEAXI_SR       (*(volatile uint32_t *)(CACHEAXI_BASE + 0x04))
#define CACHEAXI_FCR      (*(volatile uint32_t *)(CACHEAXI_BASE + 0x0C))
#define CACHEAXI_CR2      (*(volatile uint32_t *)(CACHEAXI_BASE + 0x100))
#define CACHEAXI_RSADDR   (*(volatile uint32_t *)(CACHEAXI_BASE + 0x104))
#define CACHEAXI_READDR   (*(volatile uint32_t *)(CACHEAXI_BASE + 0x108))

#define SR_BUSYF     (1 << 0)
#define SR_BUSYCMDF  (1 << 3)
#define SR_CMDENDF   (1 << 4)
#define CR1_EN       (1 << 0)
#define CR1_CACHEINV (1 << 1)
#define CR2_STARTCMD (1 << 0)
#define CR2_CLEAN    (1 << 1)
#define CR2_CLEANINV ((1 << 1) | (1 << 2))
#define FCR_CLEAR    0x12

static bool g_npu_cache_initialized = false;

void npu_cache_init(void)
{
  g_npu_cache_initialized = true;
}

void npu_cache_deinit(void)
{
  g_npu_cache_initialized = false;
}

void npu_cache_enable(void)
{
  if (!g_npu_cache_initialized)
    {
      npu_cache_init();
    }

  /* Cache is already enabled by bringup.c NPU init */
}

void npu_cache_disable(void)
{
  CACHEAXI_CR1 &= ~CR1_EN;
}

void npu_cache_invalidate(void)
{
  CACHEAXI_CR1 |= CR1_CACHEINV;

  while (CACHEAXI_SR & SR_BUSYF);
}

static void cacheaxi_cmd_range(uint32_t start, uint32_t end, uint32_t cmd)
{
  volatile int timeout;

  /* Wait for any pending command */

  timeout = 100000;
  while ((CACHEAXI_SR & SR_BUSYCMDF) && --timeout > 0);

  /* Clear flags */

  CACHEAXI_FCR = FCR_CLEAR;

  /* Set address range */

  CACHEAXI_RSADDR = start;
  CACHEAXI_READDR = end - 1;

  /* Set command + start */

  CACHEAXI_CR2 = (CACHEAXI_CR2 & ~0x07) | cmd | CR2_STARTCMD;

  /* Poll for completion */

  timeout = 100000;
  while (!(CACHEAXI_SR & SR_CMDENDF) && --timeout > 0);

  /* Clear flags */

  CACHEAXI_FCR = FCR_CLEAR;
}

void npu_cache_clean_range(uint32_t start, uint32_t end)
{
  cacheaxi_cmd_range(start, end, CR2_CLEAN);
}

void npu_cache_clean_invalidate_range(uint32_t start, uint32_t end)
{
  cacheaxi_cmd_range(start, end, CR2_CLEANINV);
}

/* Weak callbacks for clock/reset — already done in NuttX bringup.c */

__attribute__((weak))
void npu_cache_enable_clocks_and_reset(void)
{
  /* NPU and CACHEAXI clocks already enabled by stm32_bringup.c */
}

__attribute__((weak))
void npu_cache_disable_clocks_and_reset(void)
{
  /* No-op */
}
