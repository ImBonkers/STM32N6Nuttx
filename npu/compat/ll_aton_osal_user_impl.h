/**
 * LL_ATON OSAL User Implementation for NuttX.
 *
 * This file is included by ll_aton_osal.h when LL_ATON_OSAL == LL_ATON_OSAL_USER_IMPL.
 * Defines the OSAL macros for NuttX semaphore-based async mode.
 */

#ifndef __LL_ATON_OSAL_USER_IMPL_H
#define __LL_ATON_OSAL_USER_IMPL_H

/* Functions implemented in ll_aton_osal_nuttx.c */
void aton_osal_nuttx_init(void);
void aton_osal_nuttx_deinit(void);
void aton_osal_nuttx_wfe(void);
void aton_osal_nuttx_signal_event(void);
void LL_ATON_OSAL_INSTALL_IRQ_FUNC(int irq_line, void (*handler)(void));
void LL_ATON_OSAL_ENABLE_IRQ_FUNC(int irq_line);
void LL_ATON_OSAL_DISABLE_IRQ_FUNC(int irq_line);

/* Init/deinit */
#define LL_ATON_OSAL_INIT()         aton_osal_nuttx_init()
#define LL_ATON_OSAL_DEINIT()       aton_osal_nuttx_deinit()

/* WFE: block on semaphore (yields to NuttX scheduler) */
#define LL_ATON_OSAL_WFE()          aton_osal_nuttx_wfe()
#define LL_ATON_OSAL_SIGNAL_EVENT() aton_osal_nuttx_signal_event()

/* IRQ management */
#define LL_ATON_OSAL_INSTALL_IRQ(irq_aton_line_nr, handler)  \
  LL_ATON_OSAL_INSTALL_IRQ_FUNC(irq_aton_line_nr, handler)
#define LL_ATON_OSAL_REMOVE_IRQ(irq_aton_line_nr)            \
  LL_ATON_OSAL_INSTALL_IRQ_FUNC(irq_aton_line_nr, (void *)0)
#define LL_ATON_OSAL_ENABLE_IRQ(irq_aton_line_nr)            \
  LL_ATON_OSAL_ENABLE_IRQ_FUNC(irq_aton_line_nr)
#define LL_ATON_OSAL_DISABLE_IRQ(irq_aton_line_nr)           \
  LL_ATON_OSAL_DISABLE_IRQ_FUNC(irq_aton_line_nr)

/* Critical section: disable/enable NPU IRQ */
#define LL_ATON_OSAL_ENTER_CS()     LL_ATON_OSAL_DISABLE_IRQ_FUNC(0)
#define LL_ATON_OSAL_EXIT_CS()      LL_ATON_OSAL_ENABLE_IRQ_FUNC(0)

/* No parallel networks */
#ifndef APP_HAS_PARALLEL_NETWORKS
#define APP_HAS_PARALLEL_NETWORKS 0
#endif

#endif /* __LL_ATON_OSAL_USER_IMPL_H */
