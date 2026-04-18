/**
 * LL_ATON OSAL for NuttX — IRQ-driven WFE with manual triggered_events.
 *
 * ISR sets flag + disables NVIC (zero kernel API calls).
 * WFE uses ARM WFE instruction for low-power wait until IRQ fires.
 * After wakeup, manually sets triggered_events from STRENG IRQ regs
 * (ATON_STD_IRQHandler doesn't work — INTREG bit mapping mismatch).
 */

#include <nuttx/config.h>

#ifdef CONFIG_STM32N6_NPU

#include <nuttx/irq.h>
#include <nuttx/arch.h>

#include "ll_aton_osal_nuttx.h"
#include "ll_aton_platform.h"
#include "ll_aton_NN_interface.h"

#define NPU_NX_IRQ    (16 + 53)

#define STRENG_BASE   0x580E5000
#define STRENG_STRIDE 0x1000
#define STRENG_COUNT  10

static void (*g_aton_isr)(void);
static volatile bool g_npu_irq_fired;

int npu_irq_handler(int irq, void *context, void *arg)
{
  (void)irq;
  (void)context;
  (void)arg;

  g_npu_irq_fired = true;
  up_disable_irq(NPU_NX_IRQ);

  return 0;
}

void aton_osal_nuttx_init(void)
{
  irq_attach(NPU_NX_IRQ, npu_irq_handler, NULL);
}

void aton_osal_nuttx_deinit(void)
{
}

void aton_osal_nuttx_wfe(void)
{
  extern NN_Instance_TypeDef *volatile __ll_current_aton_ip_owner;
  NN_Instance_TypeDef *owner = __ll_current_aton_ip_owner;
  uint32_t wait_mask;
  int i;

  if (owner == NULL || owner->exec_state.current_epoch_block == NULL)
    {
      return;
    }

  wait_mask = owner->exec_state.current_epoch_block->wait_mask;

  /* Wait for NPU IRQ (low-power, near-zero CPU) */

  g_npu_irq_fired = false;
  up_enable_irq(NPU_NX_IRQ);

  while (!g_npu_irq_fired)
    {
      __asm__ volatile ("wfe");
    }

  /* Set triggered_events from STRENG IRQ registers */

  {
    uint32_t events = owner->exec_state.triggered_events;

    for (i = 0; i < STRENG_COUNT; i++)
      {
        if (wait_mask & (1u << i))
          {
            uint32_t base = STRENG_BASE + STRENG_STRIDE * i;
            uint32_t irq_reg = *(volatile uint32_t *)(base + 0x3c);
            *(volatile uint32_t *)(base + 0x3c) = irq_reg;
            events |= (1u << i);
          }
      }

    owner->exec_state.triggered_events = events;
  }
}

void aton_osal_nuttx_signal_event(void)
{
}

void LL_ATON_OSAL_INSTALL_IRQ_FUNC(int irq_line, void (*handler)(void))
{
  (void)irq_line;
  g_aton_isr = handler;
}

void LL_ATON_OSAL_ENABLE_IRQ_FUNC(int irq_line)
{
  (void)irq_line;
}

void LL_ATON_OSAL_DISABLE_IRQ_FUNC(int irq_line)
{
  (void)irq_line;
}

#endif /* CONFIG_STM32N6_NPU */
