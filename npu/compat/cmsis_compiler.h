/**
 * Minimal CMSIS compiler shim for LL_ATON on NuttX.
 * Provides __STATIC_FORCEINLINE and similar compiler attributes.
 */

#ifndef __CMSIS_COMPILER_H
#define __CMSIS_COMPILER_H

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

#ifndef __NO_RETURN
#define __NO_RETURN __attribute__((__noreturn__))
#endif

#ifndef __USED
#define __USED __attribute__((used))
#endif

#ifndef __COMPILER_BARRIER
#define __COMPILER_BARRIER() __asm volatile ("" ::: "memory")
#endif

#endif /* __CMSIS_COMPILER_H */
