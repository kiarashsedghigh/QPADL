#ifndef POLYVEC_H
#define POLYVEC_H

#include <stdint.h>
#include "params.h"
#include "poly.h"

/* Vectors of polynomials of length L */
typedef struct {
  poly vec[L];
} polyvecl;

#define polyvecl_uniform_eta DILITHIUM_NAMESPACE(polyvecl_uniform_eta)
__host__ __device__ void polyvecl_uniform_eta(polyvecl *v, const uint8_t seed[CRHBYTES], uint16_t nonce);

#define polyvecl_uniform_gamma1 DILITHIUM_NAMESPACE(polyvecl_uniform_gamma1)
__host__ __device__ void polyvecl_uniform_gamma1(polyvecl *v, const uint8_t seed[CRHBYTES], uint16_t nonce);

#define polyvecl_reduce DILITHIUM_NAMESPACE(polyvecl_reduce)
__host__ __device__ void polyvecl_reduce(polyvecl *v);

#define polyvecl_add DILITHIUM_NAMESPACE(polyvecl_add)
__host__ __device__ void polyvecl_add(polyvecl *w, const polyvecl *u, const polyvecl *v);

#define polyvecl_ntt DILITHIUM_NAMESPACE(polyvecl_ntt)
__host__ __device__ void polyvecl_ntt(polyvecl *v);
#define polyvecl_invntt_tomont DILITHIUM_NAMESPACE(polyvecl_invntt_tomont)
__host__ __device__ void polyvecl_invntt_tomont(polyvecl *v);
#define polyvecl_pointwise_poly_montgomery DILITHIUM_NAMESPACE(polyvecl_pointwise_poly_montgomery)
__host__ __device__ void polyvecl_pointwise_poly_montgomery(polyvecl *r, const poly *a, const polyvecl *v);
#define polyvecl_pointwise_acc_montgomery \
        DILITHIUM_NAMESPACE(polyvecl_pointwise_acc_montgomery)
__host__ __device__ void polyvecl_pointwise_acc_montgomery(poly *w,
                                       const polyvecl *u,
                                       const polyvecl *v);


#define polyvecl_chknorm DILITHIUM_NAMESPACE(polyvecl_chknorm)
__host__ __device__ int polyvecl_chknorm(const polyvecl *v, int32_t B);



/* Vectors of polynomials of length K */
typedef struct {
  poly vec[K];
} polyveck;

#define polyveck_uniform_eta DILITHIUM_NAMESPACE(polyveck_uniform_eta)
__host__ __device__ void polyveck_uniform_eta(polyveck *v, const uint8_t seed[CRHBYTES], uint16_t nonce);

#define polyveck_reduce DILITHIUM_NAMESPACE(polyveck_reduce)
__host__ __device__ void polyveck_reduce(polyveck *v);
#define polyveck_caddq DILITHIUM_NAMESPACE(polyveck_caddq)
__host__ __device__ void polyveck_caddq(polyveck *v);

#define polyveck_add DILITHIUM_NAMESPACE(polyveck_add)
__host__ __device__ void polyveck_add(polyveck *w, const polyveck *u, const polyveck *v);
#define polyveck_sub DILITHIUM_NAMESPACE(polyveck_sub)
__host__ __device__ void polyveck_sub(polyveck *w, const polyveck *u, const polyveck *v);
#define polyveck_shiftl DILITHIUM_NAMESPACE(polyveck_shiftl)
__host__ __device__ void polyveck_shiftl(polyveck *v);

#define polyveck_ntt DILITHIUM_NAMESPACE(polyveck_ntt)
__host__ __device__ void polyveck_ntt(polyveck *v);
#define polyveck_invntt_tomont DILITHIUM_NAMESPACE(polyveck_invntt_tomont)
__host__ __device__ void polyveck_invntt_tomont(polyveck *v);
#define polyveck_pointwise_poly_montgomery DILITHIUM_NAMESPACE(polyveck_pointwise_poly_montgomery)
__host__ __device__ void polyveck_pointwise_poly_montgomery(polyveck *r, const poly *a, const polyveck *v);

#define polyveck_chknorm DILITHIUM_NAMESPACE(polyveck_chknorm)
__host__ __device__ int polyveck_chknorm(const polyveck *v, int32_t B);

#define polyveck_power2round DILITHIUM_NAMESPACE(polyveck_power2round)
__host__ __device__ void polyveck_power2round(polyveck *v1, polyveck *v0, const polyveck *v);
#define polyveck_decompose DILITHIUM_NAMESPACE(polyveck_decompose)
__host__ __device__ void polyveck_decompose(polyveck *v1, polyveck *v0, const polyveck *v);
#define polyveck_make_hint DILITHIUM_NAMESPACE(polyveck_make_hint)
__host__ __device__ unsigned int polyveck_make_hint(polyveck *h,
                                const polyveck *v0,
                                const polyveck *v1);
#define polyveck_use_hint DILITHIUM_NAMESPACE(polyveck_use_hint)
__host__ __device__ void polyveck_use_hint(polyveck *w, const polyveck *v, const polyveck *h);

#define polyveck_pack_w1 DILITHIUM_NAMESPACE(polyveck_pack_w1)
__host__ __device__ void polyveck_pack_w1(uint8_t r[K*POLYW1_PACKEDBYTES], const polyveck *w1);

#define polyvec_matrix_expand DILITHIUM_NAMESPACE(polyvec_matrix_expand)
__host__ __device__ void polyvec_matrix_expand(polyvecl mat[K], const uint8_t rho[SEEDBYTES]);

#define polyvec_matrix_pointwise_montgomery DILITHIUM_NAMESPACE(polyvec_matrix_pointwise_montgomery)
__host__ __device__ void polyvec_matrix_pointwise_montgomery(polyveck *t, const polyvecl mat[K], const polyvecl *v);

#endif
