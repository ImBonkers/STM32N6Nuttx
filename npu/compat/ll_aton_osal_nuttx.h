/**
 * LL_ATON OSAL for NuttX — semaphore-based WFE for async mode.
 *
 * Provides the macros needed by ll_aton_osal_rtos_template.c to
 * implement WFE/SIGNAL_EVENT using NuttX semaphores and IRQ API.
 */

#ifndef __LL_ATON_OSAL_NUTTX_H
#define __LL_ATON_OSAL_NUTTX_H

#include <nuttx/config.h>
#include <semaphore.h>
#include <nuttx/irq.h>
#include <stdbool.h>

#include "ll_aton_osal.h"

#ifdef __cplusplus
extern "C"
{
#endif

/* No parallel networks */
#ifndef APP_HAS_PARALLEL_NETWORKS
#define APP_HAS_PARALLEL_NETWORKS 0
#endif

/* API functions (implemented in ll_aton_osal_nuttx.c) */
void aton_osal_nuttx_init(void);
void aton_osal_nuttx_deinit(void);
void aton_osal_nuttx_wfe(void);
void aton_osal_nuttx_signal_event(void);

/* Type macros — NuttX uses POSIX sem_t */
#define _WfeSemaphoreType_      sem_t
#define _CacheMutexType_        sem_t
#define _ReturnType_            int
#define _OsTrue_                0
#define _NullHandle_            NULL

/* Semaphore create: initialize counting semaphore */
#define _CreateWfeSemaphore_(_sem, _static)   \
  sem_init(&(_sem), 0, 0)

#define _CreateCacheMutex_(_sem, _static)     \
  sem_init(&(_sem), 0, 1)

/* WFE semaphore operations */
#define _MakeWfeSemaphoreUnavailable_(_sem)
#define _GetWfeSemaphore_(_sem)               sem_wait(&(_sem))
#define _ReleaseWfeSemaphore_(_sem)           sem_post(&(_sem))
#define _ReleaseWfeSemaphoreISR_(_sem, ...)   sem_post(&(_sem))

/* Cache mutex operations */
#define _MakeCacheMutexAvailable_(_sem)
#define _GetCacheMutex_(_sem)                 sem_wait(&(_sem))
#define _ReleaseCacheMutex_(_sem)             sem_post(&(_sem))

/* IRQ finalization — set priority (no-op for NuttX, priority set at attach) */
#define _FinalizeIRQHandling_()

/* Preemption control — use sched_lock/unlock */
#define _DisablePreemption_()  sched_lock()
#define _EnablePreemption_()   sched_unlock()

/* Non-DAO init/deinit — nothing needed */
#define _InitNonDao_()
#define _DeInitNonDao_()

/* ISR head/tail code — NuttX handles context switch automatically */
#define _HeadIsrCode_(...)
#define _TailIsrCode_(...)

/* Signal from ISR context */
#define RTOS_HAS_NO_ISR_SIGNAL

#ifdef __cplusplus
}
#endif

#endif /* __LL_ATON_OSAL_NUTTX_H */
