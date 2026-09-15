/**
 * LL_ATON OSAL for NuttX — IRQ-driven WFE with manual triggered_events.
 *
 * ISR sets flag + disables NVIC (zero kernel API calls).
 * The wait spins on that flag — WFE/WFI stalls the NPU mid-epoch.
 * After wakeup, manually sets triggered_events from STRENG IRQ regs
 * (ATON_STD_IRQHandler doesn't work — INTREG bit mapping mismatch).
 */

#include <nuttx/config.h>

#ifdef CONFIG_STM32N6_NPU

#include <nuttx/irq.h>
#include <nuttx/arch.h>
#include <nuttx/clock.h>
#include <debug.h>

#include "ll_aton_osal_nuttx.h"
#include "ll_aton_platform.h"
#include "ll_aton_NN_interface.h"

#define NPU_NX_IRQ    (16 + 53)

#define STRENG_BASE   0x580E5000
#define STRENG_STRIDE 0x1000
#define STRENG_COUNT  10

/* Bound the wait so a missing IRQ reports instead of wedging the board. */

#define NPU_WFE_TIMEOUT_TICKS  MSEC2TICK(2000)

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

  /* Wait for every stream engine in wait_mask to finish.
   *
   * Completion is CTRL bit 31 (RUNNING) clearing — the same test the
   * polling implementation uses.  The per-engine IRQ status register is
   * NOT a reliable "epoch done" signal: crediting the mask from it lets
   * the runtime advance over in-flight transfers, which yields
   * non-deterministic, corrupt activations.
   *
   * The IRQ only provides the wakeup, so registers are read once per
   * interrupt rather than in a tight loop — that avoids the AXI traffic
   * that makes sched_yield() polling contend with the NPU.
   *
   * Never sleep here: WFE/WFI stalls the NPU mid-epoch regardless of the
   * LPEN sleep-clock bits, so the wait spins on a flag in SRAM.
   */

  {
    uint32_t done = 0;
    clock_t start = clock_systime_ticks();
    bool timed_out = false;

    while ((done & wait_mask) != wait_mask && !timed_out)
      {
        for (i = 0; i < STRENG_COUNT; i++)
          {
            uint32_t bit = 1u << i;

            if ((wait_mask & bit) != 0 && (done & bit) == 0)
              {
                uint32_t ctrl = *(volatile uint32_t *)
                                (STRENG_BASE + STRENG_STRIDE * i);

                if ((ctrl & (1u << 31)) == 0)
                  {
                    done |= bit;
                  }
              }
          }

        if ((done & wait_mask) == wait_mask)
          {
            break;
          }

        g_npu_irq_fired = false;
        up_enable_irq(NPU_NX_IRQ);

        while (!g_npu_irq_fired)
          {
            __asm__ volatile ("nop");

            if ((clock_systime_ticks() - start) > NPU_WFE_TIMEOUT_TICKS)
              {
                up_disable_irq(NPU_NX_IRQ);
                syslog(LOG_ERR, "NPU: epoch wait timed out "
                       "(mask=0x%08lx done=0x%08lx)\n",
                       (unsigned long)wait_mask, (unsigned long)done);
                timed_out = true;
                break;
              }
          }
      }

    up_disable_irq(NPU_NX_IRQ);
  }

  /* Acknowledge the STRENG IRQs and publish triggered_events. */

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
