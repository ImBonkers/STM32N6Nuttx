/**
 * LL_ATON OSAL for NuttX — IRQ and semaphore management.
 *
 * Implements INSTALL_IRQ, ENABLE_IRQ, DISABLE_IRQ, WFE, SIGNAL_EVENT
 * for NuttX using irq_attach() and POSIX semaphores.
 */

#include <nuttx/config.h>

#ifdef CONFIG_STM32N6_NPU

#include <semaphore.h>
#include <nuttx/irq.h>
#include <nuttx/arch.h>
#include <debug.h>

#include "ll_aton_osal_nuttx.h"
#include "ll_aton_platform.h"
#include "ll_aton_NN_interface.h"

/* NPU IRQ numbers: NPU0=53, NPU1=54, NPU2=55, NPU3=56 */

#define NPU_IRQ_BASE  53

/* Stored ISR handler from the LL_ATON runtime */

static void (*g_aton_isr)(void);
static sem_t g_wfe_sem;
static bool  g_initialized;

/**
 * NuttX IRQ handler — dispatches to LL_ATON's ATON_STD_IRQHandler
 */

static int npu_irq_handler(int irq, void *context, void *arg)
{
  (void)irq;
  (void)context;
  (void)arg;

  if (g_aton_isr != NULL)
    {
      g_aton_isr();
    }

  return 0;
}

void aton_osal_nuttx_init(void)
{
  if (!g_initialized)
    {
      sem_init(&g_wfe_sem, 0, 0);
      g_initialized = true;
    }
}

void aton_osal_nuttx_deinit(void)
{
  if (g_initialized)
    {
      sem_destroy(&g_wfe_sem);
      g_initialized = false;
    }
}

void aton_osal_nuttx_wfe(void)
{
  /* INTCTRL hardware interrupt routing to NVIC doesn't work on
   * STM32N6 (STRENG completion events never reach INTCTRL INTREG).
   * Instead, poll STRENG CTRL.RUNNING with sched_yield() to let
   * the NuttX scheduler run other tasks, then manually set
   * triggered_events like the ISR would.
   */

  extern NN_Instance_TypeDef *volatile __ll_current_aton_ip_owner;
  NN_Instance_TypeDef *owner = __ll_current_aton_ip_owner;
  uint32_t wait_mask;
  int i;

  if (owner == NULL || owner->exec_state.current_epoch_block == NULL)
    {
      return;
    }

  wait_mask = owner->exec_state.current_epoch_block->wait_mask;

  /* Poll STRENGs in wait_mask until all complete */

  while (true)
    {
      uint32_t running = 0;

      for (i = 0; i < 10; i++)
        {
          if (wait_mask & (1u << i))
            {
              uint32_t ctrl = *(volatile uint32_t *)
                (0x580E5000 + 0x1000 * i);
              if (ctrl & (1u << 31))  /* RUNNING bit */
                {
                  running |= (1u << i);
                }
            }
        }

      if (!running)
        {
          break;
        }

      sched_yield();
    }

  /* Set triggered_events (like ATON_STD_IRQHandler would) */

  {
    uint32_t events = owner->exec_state.triggered_events;

    for (i = 0; i < 10; i++)
      {
        if (wait_mask & (1u << i))
          {
            /* Acknowledge STRENG IRQ */

            uint32_t base = 0x580E5000 + 0x1000 * i;
            uint32_t irq = *(volatile uint32_t *)(base + 0x3C);
            *(volatile uint32_t *)(base + 0x3C) = irq;

            events |= (1u << i);
          }
      }

    owner->exec_state.triggered_events = events;
  }
}

void aton_osal_nuttx_signal_event(void)
{
  sem_post(&g_wfe_sem);
}

/**
 * OSAL IRQ management — called by LL_ATON runtime
 */

void LL_ATON_OSAL_INSTALL_IRQ_FUNC(int irq_line, void (*handler)(void))
{
  int irq = NPU_IRQ_BASE + irq_line;

  g_aton_isr = handler;
  (void)irq;

  if (handler != NULL)
    {
      irq_attach(irq + 16, npu_irq_handler, NULL);
      up_enable_irq(irq + 16);
    }
  else
    {
      up_disable_irq(irq + 16);
      irq_attach(irq + 16, NULL, NULL);
    }
}

void LL_ATON_OSAL_ENABLE_IRQ_FUNC(int irq_line)
{
  up_enable_irq(NPU_IRQ_BASE + irq_line + 16);
}

void LL_ATON_OSAL_DISABLE_IRQ_FUNC(int irq_line)
{
  up_disable_irq(NPU_IRQ_BASE + irq_line + 16);
}

#endif /* CONFIG_STM32N6_NPU */
