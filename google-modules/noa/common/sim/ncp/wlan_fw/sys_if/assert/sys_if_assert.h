#ifndef SYS_IF_ASSERT_SYS_IF_ASSERT_H
#define SYS_IF_ASSERT_SYS_IF_ASSERT_H

#if defined(__KERNEL__)
#include <linux/bug.h>
#else
#include "pw_assert/check.h"
#endif

#if defined(__KERNEL__)
#define SYS_IF_ASSERT(condition) BUG_ON(condition)
#else
#define SYS_IF_ASSERT(condition) PW_CHECK(condition)
#endif

#endif // SYS_IF_ASSERT_SYS_IF_ASSERT_H
