#ifndef RANDOMBYTES_H
#define RANDOMBYTES_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>

void randombytes(uint8_t *out, size_t outlen);


#ifdef __cplusplus
}
#endif
#endif
