#ifndef SYS_IF_TYPES_TYPES_H
#define SYS_IF_TYPES_TYPES_H

#if defined(__KERNEL__)
#include <linux/types.h>
#else
#include <cstdint>
#include <cinttypes>
#endif /* defined(__KERNEL__) */

#if defined(__KERNEL__)
#define PRId8 "d"
#define PRIi8 "i"
#define PRIu8 "u"
#define PRIx8 "x"
#define PRIX8 "X"

#define PRId16 "d"
#define PRIi16 "i"
#define PRIu16 "u"
#define PRIx16 "x"
#define PRIX16 "X"

#define PRId32 "d"
#define PRIi32 "i"
#define PRIu32 "u"
#define PRIx32 "x"
#define PRIX32 "X"

#define PRId64 "lld"
#define PRIi64 "lli"
#define PRIu64 "llu"
#define PRIx64 "llx"
#define PRIX64 "llX"
#endif /* defined(__KERNEL__) */

#endif /* SYS_IF_TYPES_TYPES_H */
