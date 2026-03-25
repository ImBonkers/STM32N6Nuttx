/**
 * Compatibility shim for LL_ATON runtime on NuttX.
 *
 * LL_ATON expects CMSIS stm32n6xx.h which NuttX doesn't provide.
 * This header provides the minimal subset needed:
 *   - NPU/CACHEAXI base addresses
 *   - IRQ numbers
 *   - ARM intrinsics (__WFE, __DSB, __ISB)
 *   - NVIC stubs (polling mode doesn't need real NVIC)
 *   - MEMSYSCTL for cache power control
 */

#ifndef __STM32N6XX_COMPAT_H
#define __STM32N6XX_COMPAT_H

#include <stdint.h>
#include <stdbool.h>

/* CPU is in TrustZone Secure state */

#define CPU_IN_SECURE_STATE

/* Peripheral base addresses (Secure aliases) */

#define NPU_BASE_S        0x580E0000UL
#define NPU_BASE_NS       0x480E0000UL
#define CACHEAXI_BASE_S   0x580DFC00UL
#define CACHEAXI_BASE_NS  0x480DFC00UL

/* CACHEAXI typedef for npu_cache compatibility */

typedef struct
{
  volatile uint32_t CR1;
  volatile uint32_t SR;
  volatile uint32_t IER;
  volatile uint32_t FCR;
  uint32_t RESERVED1[60];
  volatile uint32_t CR2;
  volatile uint32_t CMDRSADDRR;
  volatile uint32_t CMDREADDRR;
} CACHEAXI_TypeDef;

#ifdef CPU_IN_SECURE_STATE
#define CACHEAXI   ((CACHEAXI_TypeDef *)CACHEAXI_BASE_S)
#else
#define CACHEAXI   ((CACHEAXI_TypeDef *)CACHEAXI_BASE_NS)
#endif

/* MEMSYSCTL for I-cache / D-cache power-on */

typedef struct
{
  volatile uint32_t MSCR;
} MEMSYSCTL_TypeDef;

#define MEMSYSCTL_BASE     0xE001E000UL
#define MEMSYSCTL          ((MEMSYSCTL_TypeDef *)MEMSYSCTL_BASE)
#define MEMSYSCTL_MSCR_ICACTIVE_Msk  (1UL << 0)
#define MEMSYSCTL_MSCR_DCACTIVE_Msk  (1UL << 1)

/* IRQ numbers */

typedef enum
{
  NPU0_IRQn     = 53,
  NPU1_IRQn     = 54,
  NPU2_IRQn     = 55,
  NPU3_IRQn     = 56,
  CACHEAXI_IRQn = 57,
} IRQn_Type_NPU;

/* SCB D-cache functions (NuttX provides these via armv8-m layer) */

#define SCB_CCR_DC_Msk    (1UL << 16)

typedef struct
{
  volatile uint32_t CPUID;
  volatile uint32_t ICSR;
  volatile uint32_t VTOR;
  volatile uint32_t AIRCR;
  volatile uint32_t SCR;
  volatile uint32_t CCR;
} SCB_Compat_TypeDef;

#define SCB_BASE           0xE000ED00UL
#define SCB                ((SCB_Compat_TypeDef *)SCB_BASE)

/* ARM intrinsics */

#ifndef __WFE
#define __WFE()    __asm volatile ("wfe")
#endif
#ifndef __DSB
#define __DSB()    __asm volatile ("dsb sy" ::: "memory")
#endif
#ifndef __ISB
#define __ISB()    __asm volatile ("isb sy" ::: "memory")
#endif
#ifndef __NOP
#define __NOP()    __asm volatile ("nop")
#endif

/* NVIC — route through NuttX IRQ API via OSAL functions.
 * The OSAL ENTER_CS/EXIT_CS macros call these for critical sections.
 * For async mode, we need real enable/disable; for polling, no-ops work.
 */

void LL_ATON_OSAL_ENABLE_IRQ_FUNC(int irq_line);
void LL_ATON_OSAL_DISABLE_IRQ_FUNC(int irq_line);

static inline void NVIC_EnableIRQ(int irq)  { (void)irq; }
static inline void NVIC_DisableIRQ(int irq) { (void)irq; }
static inline void NVIC_SetPriority(int irq, uint32_t prio) { (void)irq; (void)prio; }

/* D-cache maintenance via CMSIS-style API (used by mcu_cache.c) */

static inline void SCB_InvalidateDCache(void)
{
  /* NuttX: up_invalidate_dcache_all() — but we use set/way directly */
  __asm volatile ("dsb sy");
}

static inline void SCB_CleanDCache(void)
{
  __asm volatile ("dsb sy");
}

static inline void SCB_CleanInvalidateDCache(void)
{
  __asm volatile ("dsb sy");
}

static inline void SCB_InvalidateDCache_by_Addr(volatile void *addr, int32_t size)
{
  (void)addr; (void)size;
  /* Will be replaced by proper NuttX up_invalidate_dcache in mcu_cache.c */
}

static inline void SCB_CleanDCache_by_Addr(volatile void *addr, int32_t size)
{
  (void)addr; (void)size;
}

static inline void SCB_CleanInvalidateDCache_by_Addr(volatile void *addr, int32_t size)
{
  (void)addr; (void)size;
}

/* Compiler attributes */

#ifndef __STATIC_FORCEINLINE
#define __STATIC_FORCEINLINE static inline __attribute__((always_inline))
#endif

#ifndef __STATIC_INLINE
#define __STATIC_INLINE static inline
#endif

#ifndef __WEAK
#define __WEAK __attribute__((weak))
#endif

#ifndef __PACKED
#define __PACKED __attribute__((packed))
#endif

#ifndef __ALIGNED
#define __ALIGNED(x) __attribute__((aligned(x)))
#endif

#endif /* __STM32N6XX_COMPAT_H */
