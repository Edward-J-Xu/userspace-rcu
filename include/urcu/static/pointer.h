// SPDX-FileCopyrightText: 2009 Mathieu Desnoyers <mathieu.desnoyers@efficios.com>
// SPDX-FileCopyrightText: 2009 Paul E. McKenney, IBM Corporation.
//
// SPDX-License-Identifier: LGPL-2.1-or-later

#ifndef _URCU_POINTER_STATIC_H
#define _URCU_POINTER_STATIC_H

/*
 * Userspace RCU header. Operations on pointers.
 *
 * TO BE INCLUDED ONLY IN CODE THAT IS TO BE RECOMPILED ON EACH LIBURCU
 * RELEASE. See urcu.h for linking dynamically with the userspace rcu library.
 *
 * IBM's contributions to this file may be relicensed under LGPLv2 or later.
 */

#include <urcu/compiler.h>
#include <urcu/arch.h>
#include <urcu/system.h>
#include <urcu/uatomic.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * _rcu_dereference - reads (copy) a RCU-protected pointer to a local variable
 * into a RCU read-side critical section. The pointer can later be safely
 * dereferenced within the critical section.
 *
 * This ensures that the pointer copy is invariant thorough the whole critical
 * section.
 *
 * Inserts memory barriers on architectures that require them (currently only
 * Alpha) and documents which pointers are protected by RCU.
 *
 * With C standards prior to C11/C++11, the compiler memory barrier in
 * CMM_LOAD_SHARED() ensures that value-speculative optimizations (e.g.
 * VSS: Value Speculation Scheduling) does not perform the data read
 * before the pointer read by speculating the value of the pointer.
 * Correct ordering is ensured because the pointer is read as a volatile
 * access. This acts as a global side-effect operation, which forbids
 * reordering of dependent memory operations.
 *
 * With C standards C11/C++11, concerns about dependency-breaking
 * optimizations are taken care of by the "memory_order_consume" atomic
 * load.
 *
 * Use the gcc __atomic_load() rather than C11/C++11 atomic load
 * explicit because the pointer used as input argument is a pointer,
 * not an _Atomic type as required by C11/C++11.
 *
 * By defining URCU_DEREFERENCE_USE_VOLATILE, the user requires use of
 * volatile access to implement rcu_dereference rather than
 * memory_order_consume load from the C11/C++11 standards.
 *
 * This may improve performance on weakly-ordered architectures where
 * the compiler implements memory_order_consume as a
 * memory_order_acquire, which is stricter than required by the
 * standard.
 *
 * Note that using volatile accesses for rcu_dereference may cause
 * LTO to generate incorrectly ordered code starting from C11/C++11.
 *
 * Should match rcu_assign_pointer() or rcu_xchg_pointer().
 *
 * This macro is less than 10 lines long.  The intent is that this macro
 * meets the 10-line criterion in LGPL, allowing this function to be
 * expanded directly in non-LGPL code.
 */

#if !defined (URCU_DEREFERENCE_USE_VOLATILE) &&		\
	((defined (__cplusplus) && __cplusplus >= 201103L) ||	\
	(defined (__STDC_VERSION__) && __STDC_VERSION__ >= 201112L))
# define __URCU_DEREFERENCE_USE_ATOMIC_CONSUME
#endif

/*
 * If p is const (the pointer itself, not what it points to), using
 * __typeof__(p) would declare a const variable, leading to
 * -Wincompatible-pointer-types errors.  Using the statement expression
 * makes it an rvalue and gets rid of the const-ness.
 */
#ifdef __URCU_DEREFERENCE_USE_ATOMIC_CONSUME
# define _rcu_dereference(p) __extension__ ({						\
				__typeof__(__extension__ ({				\
					__typeof__(p) __attribute__((unused)) _________p0 = { 0 }; \
					_________p0;					\
				})) _________p1;					\
				__atomic_load(&(p), &_________p1, __ATOMIC_CONSUME);	\
				(_________p1);						\
			})
#else
# define _rcu_dereference(p) __extension__ ({						\
				__typeof__(p) _________p1 = CMM_LOAD_SHARED(p);		\
				cmm_smp_read_barrier_depends();				\
				(_________p1);						\
			})
#endif

/**
 * _rcu_cmpxchg_pointer - same as rcu_assign_pointer, but tests if the pointer
 * is as expected by "old". If succeeds, returns the previous pointer to the
 * data structure, which can be safely freed after waiting for a quiescent state
 * using synchronize_rcu(). If fails (unexpected value), returns old (which
 * should not be freed !).
 *
 * uatomic_cmpxchg() acts as both release and acquire barriers.
 *
 * This macro is less than 10 lines long.  The intent is that this macro
 * meets the 10-line criterion in LGPL, allowing this function to be
 * expanded directly in non-LGPL code.
 */
#define _rcu_cmpxchg_pointer(p, old, _new)				\
	__extension__							\
	({								\
		__typeof__(*p) _________pold = (old);			\
		__typeof__(*p) _________pnew = (_new);			\
		uatomic_cmpxchg(p, _________pold, _________pnew);	\
	})

/**
 * _rcu_xchg_pointer - same as rcu_assign_pointer, but returns the previous
 * pointer to the data structure, which can be safely freed after waiting for a
 * quiescent state using synchronize_rcu().
 *
 * uatomic_xchg() acts as both release and acquire barriers.
 *
 * This macro is less than 10 lines long.  The intent is that this macro
 * meets the 10-line criterion in LGPL, allowing this function to be
 * expanded directly in non-LGPL code.
 */
#define _rcu_xchg_pointer(p, v)				\
	__extension__					\
	({						\
		__typeof__(*p) _________pv = (v);	\
		uatomic_xchg(p, _________pv);		\
	})


#define _rcu_set_pointer(p, v)				\
	do {						\
		__typeof__(*p) _________pv = (v);	\
		if (!__builtin_constant_p(v) || 	\
		    ((v) != NULL))			\
			cmm_wmb();				\
		uatomic_set(p, _________pv);		\
	} while (0)

/**
 * _rcu_assign_pointer - assign (publicize) a pointer to a new data structure
 * meant to be read by RCU read-side critical sections. Returns the assigned
 * value.
 *
 * Documents which pointers will be dereferenced by RCU read-side critical
 * sections and adds the required memory barriers on architectures requiring
 * them. It also makes sure the compiler does not reorder code initializing the
 * data structure before its publication.
 *
 * Should match rcu_dereference().
 *
 * This macro is less than 10 lines long.  The intent is that this macro
 * meets the 10-line criterion in LGPL, allowing this function to be
 * expanded directly in non-LGPL code.
 */
#define _rcu_assign_pointer(p, v)	_rcu_set_pointer(&(p), v)

#ifdef __cplusplus
}
#endif

#endif /* _URCU_POINTER_STATIC_H */
