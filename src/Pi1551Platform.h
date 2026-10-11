#ifndef PI1551_PLATFORM_H
#define PI1551_PLATFORM_H
// Only scheduling instructions differ in native component tests. GPIO and
// FatFS remain the actual production interfaces, supplied by observable stubs.
#ifdef PI1551_NATIVE_TEST
#include <atomic>
#include <thread>
inline void Pi1551Barrier() { std::atomic_thread_fence(std::memory_order_seq_cst); }
inline void Pi1551Wake() {}
inline void Pi1551Wait() { std::this_thread::yield(); }
extern unsigned Pi1551NativeClock();
inline unsigned Pi1551Clock() { return Pi1551NativeClock(); }
#else
inline void Pi1551Barrier() { asm volatile("dmb" ::: "memory"); }
inline void Pi1551Wake() { asm volatile("SEV" ::: "memory"); }
inline void Pi1551Wait() { asm volatile("WFE" ::: "memory"); }
inline unsigned Pi1551Clock() { return read32(ARM_SYSTIMER_CLO); }
#endif
#endif
