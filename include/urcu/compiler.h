// SPDX-FileCopyrightText: 2009 Mathieu Desnoyers <mathieu.desnoyers@efficios.com>
//
// SPDX-License-Identifier: LGPL-2.1-or-later

#ifndef _URCU_COMPILER_H
#define _URCU_COMPILER_H

/*
 * Compiler definitions.
 */

#include <stddef.h>	/* for offsetof */

#if defined __cplusplus
# include <type_traits>	/* for std::remove_cv */
#endif

#define caa_likely(x)	__builtin_expect(!!(x), 1)
#define caa_unlikely(x)	__builtin_expect(!!(x), 0)

#define	cmm_barrier()	__asm__ __volatile__ ("" : : : "memory")

/*
 * Instruct the compiler to perform only a single access to a variable
 * (prohibits merging and refetching). The compiler is also forbidden to reorder
 * successive instances of CMM_ACCESS_ONCE(), but only when the compiler is aware of
 * particular ordering. Compiler ordering can be ensured, for example, by
 * putting two CMM_ACCESS_ONCE() in separate C statements.
 *
 * This macro does absolutely -nothing- to prevent the CPU from reordering,
 * merging, or refetching absolutely anything at any time.  Its main intended
 * use is to mediate communication between process-level code and irq/NMI
 * handlers, all running on the same CPU.
 */
#define CMM_ACCESS_ONCE(x)	(*(__volatile__  __typeof__(x) *)&(x))

#ifndef caa_max
#define caa_max(a,b) ((a)>(b)?(a):(b))
#endif

#ifndef caa_min
#define caa_min(a,b) ((a)<(b)?(a):(b))
#endif

#if defined(__SIZEOF_LONG__)
#define CAA_BITS_PER_LONG	(__SIZEOF_LONG__ * 8)
#elif defined(_LP64)
#define CAA_BITS_PER_LONG	64
#else
#define CAA_BITS_PER_LONG	32
#endif

/*
 * caa_container_of - Get the address of an object containing a field.
 *
 * @ptr: pointer to the field.
 * @type: type of the object.
 * @member: name of the field within the object.
 */
#define caa_container_of(ptr, type, member)				\
	__extension__							\
	({								\
		const __typeof__(((type *) NULL)->member) * __ptr = (ptr); \
		(type *)((char *)__ptr - offsetof(type, member));	\
	})

/*
 * caa_container_of_check_null - Get the address of an object containing a field.
 *
 * @ptr: pointer to the field.
 * @type: type of the object.
 * @member: name of the field within the object.
 *
 * Return the address of the object containing the field. Return NULL if
 * @ptr is NULL.
 */
#define caa_container_of_check_null(ptr, type, member)			\
	__extension__							\
	({								\
		const __typeof__(((type *) NULL)->member) * __ptr = (ptr); \
		(__ptr) ? (type *)((char *)__ptr - offsetof(type, member)) : NULL; \
	})

#define CAA_BUILD_BUG_ON_ZERO(cond) (sizeof(struct { int:-!!(cond); }))
#define CAA_BUILD_BUG_ON(cond) ((void)CAA_BUILD_BUG_ON_ZERO(cond))

/*
 * __rcu is an annotation that documents RCU pointer accesses that need
 * to be protected by a read-side critical section. Eventually, a static
 * checker will be able to use this annotation to detect incorrect RCU
 * usage.
 */
#define __rcu

#ifdef __cplusplus
#define URCU_FORCE_CAST(_type, arg)	(reinterpret_cast<std::remove_cv<_type>::type>(arg))
#else
#define URCU_FORCE_CAST(type, arg)	((type) (arg))
#endif

#define caa_is_signed_type(type)	((type) -1 < (type) 0)

/*
 * Cast to unsigned long, sign-extending if @v is signed.
 * Note: casting to a larger type or to same type size keeps the sign of
 * the expression being cast (see C99 6.3.1.3).
 */
#define caa_cast_long_keep_sign(v)	((unsigned long) (v))

#if defined (__GNUC__) \
	&& ((__GNUC__ == 4) && (__GNUC_MINOR__ >= 5)	\
		|| __GNUC__ >= 5)
#define CDS_DEPRECATED(msg)	\
	__attribute__((deprecated(msg)))
#else
#define CDS_DEPRECATED(msg)	\
	__attribute__((deprecated))
#endif

#define CAA_ARRAY_SIZE(x)	(sizeof(x) / sizeof((x)[0]))

/*
 * URCU_GCC_VERSION is used to blacklist specific GCC versions with known
 * bugs, clang also defines these macros to an equivalent GCC version it
 * claims to support, so exclude it.
 */
#if defined(__GNUC__) && !defined(__clang__)
# define URCU_GCC_VERSION	(__GNUC__ * 10000 \
				+ __GNUC_MINOR__ * 100 \
				+ __GNUC_PATCHLEVEL__)
#endif

#endif /* _URCU_COMPILER_H */
