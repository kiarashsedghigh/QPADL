#ifndef REDUCE_H
#define REDUCE_H

#include <stdint.h>
#include "params.h"

#define MONT -4186625 // 2^32 % Q
#define QINV 58728449 // q^(-1) mod 2^32

#define montgomery_reduce DILITHIUM_NAMESPACE(montgomery_reduce)
__host__ __device__ int32_t montgomery_reduce(int64_t a);

#define reduce32 DILITHIUM_NAMESPACE(reduce32)
__host__ __device__ int32_t reduce32(int32_t a);

#define caddq DILITHIUM_NAMESPACE(caddq)
__host__ __device__ int32_t caddq(int32_t a);

#define freeze DILITHIUM_NAMESPACE(freeze)
__host__ __device__ int32_t freeze(int32_t a);

#endif
