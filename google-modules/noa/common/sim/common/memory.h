#ifndef __NOA_COMMON_MEMORY_H__
#define __NOA_COMMON_MEMORY_H__

// Invalidates CPU data cache line by address.
#define InvalidateDCache(...)
// Writes back CPU data cache line by address.
#define CleanDCache(...)
// Cleans and validates CPU data cache line by address.
#define FlushDCache(...)

#endif /* __NOA_COMMON_MEMORY_H__ */

