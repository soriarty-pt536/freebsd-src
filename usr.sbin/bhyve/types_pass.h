#ifndef _TYPES_H_
#define _TYPES_H_

#include "macros_pass.h"
#include <stdint.h>
#include <stdarg.h>
#include <sched.h>
#include <sys/types.h>

// typedef cpu_set_t cpuset_t;

#define ARRAY_SIZE(a) (sizeof(a) / sizeof(a[0]))

#define container_of(ptr, type, member) ({                   \
	const typeof(((type *)0)->member) * __mptr = (ptr);  \
	(type *)((char *)__mptr - (offsetof(type, member))); \
})

#define __section(x)		__attribute__((__section__(x)))

#define DATA_SET(set, sym)  __MAKE_SET(set, sym)

#define SET_FOREACH(pvar, set)                      \
	for (pvar = SET_BEGIN(set); pvar < SET_LIMIT(set); pvar++)

#define nitems(x) (sizeof((x)) / sizeof((x)[0]))
#define roundup2(x, y)  (((x)+((y)-1))&(~((y)-1)))
#define rounddown2(x, y) ((x)&(~((y)-1)))

//static inline int
//flsl(uint64_t mask)
//{
//	return mask ? 64 - __builtin_clzl(mask) : 0;
//}

/* memory barrier */
#define mb()    ({ asm volatile("mfence" ::: "memory"); (void)0; })

static inline void
do_cpuid(u_int ax, u_int *p)
{
	__asm __volatile("cpuid"
	 : "=a" (p[0]), "=b" (p[1]), "=c" (p[2]), "=d" (p[3])
	 :  "0" (ax));
}

#define UGETW(w)            \
	((w)[0] |             \
	(((uint16_t)((w)[1])) << 8))

#define UGETDW(w)           \
	((w)[0] |             \
	(((uint16_t)((w)[1])) << 8) |     \
	(((uint32_t)((w)[2])) << 16) |    \
	(((uint32_t)((w)[3])) << 24))

#define UGETQW(w)           \
	((w)[0] |             \
	(((uint16_t)((w)[1])) << 8) |     \
	(((uint32_t)((w)[2])) << 16) |    \
	(((uint32_t)((w)[3])) << 24) |    \
	(((uint64_t)((w)[4])) << 32) |    \
	(((uint64_t)((w)[5])) << 40) |    \
	(((uint64_t)((w)[6])) << 48) |    \
	(((uint64_t)((w)[7])) << 56))

#define USETW(w, v) do {         \
	  (w)[0] = (uint8_t)(v);        \
	  (w)[1] = (uint8_t)((v) >> 8);     \
} while (0)

#define USETDW(w, v) do {        \
	  (w)[0] = (uint8_t)(v);        \
	  (w)[1] = (uint8_t)((v) >> 8);     \
	  (w)[2] = (uint8_t)((v) >> 16);    \
	  (w)[3] = (uint8_t)((v) >> 24);    \
} while (0)

#define USETQW(w, v) do {        \
	  (w)[0] = (uint8_t)(v);        \
	  (w)[1] = (uint8_t)((v) >> 8);     \
	  (w)[2] = (uint8_t)((v) >> 16);    \
	  (w)[3] = (uint8_t)((v) >> 24);    \
	  (w)[4] = (uint8_t)((v) >> 32);    \
	  (w)[5] = (uint8_t)((v) >> 40);    \
	  (w)[6] = (uint8_t)((v) >> 48);    \
	  (w)[7] = (uint8_t)((v) >> 56);    \
} while (0)

#endif
