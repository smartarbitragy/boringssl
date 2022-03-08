/* Copyright (C) 1995-1998 Eric Young (eay@cryptsoft.com)
 * All rights reserved.
 *
 * This package is an SSL implementation written
 * by Eric Young (eay@cryptsoft.com).
 * The implementation was written so as to conform with Netscapes SSL.
 *
 * This library is free for commercial and non-commercial use as long as
 * the following conditions are aheared to.  The following conditions
 * apply to all code found in this distribution, be it the RC4, RSA,
 * lhash, DES, etc., code; not just the SSL code.  The SSL documentation
 * included with this distribution is covered by the same copyright terms
 * except that the holder is Tim Hudson (tjh@cryptsoft.com).
 *
 * Copyright remains Eric Young's, and as such any Copyright notices in
 * the code are not to be removed.
 * If this package is used in a product, Eric Young should be given attribution
 * as the author of the parts of the library used.
 * This can be in the form of a textual message at program startup or
 * in documentation (online or textual) provided with the package.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *    "This product includes cryptographic software written by
 *     Eric Young (eay@cryptsoft.com)"
 *    The word 'cryptographic' can be left out if the rouines from the library
 *    being used are not cryptographic related :-).
 * 4. If you include any Windows specific code (or a derivative thereof) from
 *    the apps directory (application code) you must include an acknowledgement:
 *    "This product includes software written by Tim Hudson (tjh@cryptsoft.com)"
 *
 * THIS SOFTWARE IS PROVIDED BY ERIC YOUNG ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * The licence and distribution terms for any publically available version or
 * derivative of this code cannot be changed.  i.e. this code cannot simply be
 * copied and put under another distribution licence
 * [including the GNU Public Licence.] */

#include <openssl/asn1.h>

#include <string.h>
#include <limits.h>

#include <openssl/bytestring.h>
#include <openssl/err.h>
#include <openssl/mem.h>

#include "../internal.h"


ASN1_INTEGER *ASN1_INTEGER_dup(const ASN1_INTEGER *x)
{
    return ASN1_STRING_dup(x);
}

int ASN1_INTEGER_cmp(const ASN1_INTEGER *x, const ASN1_INTEGER *y)
{
    /* Compare signs. */
    int neg = x->type & V_ASN1_NEG;
    if (neg != (y->type & V_ASN1_NEG)) {
        return neg ? -1 : 1;
    }

    int ret = ASN1_STRING_cmp(x, y);
    if (neg) {
        /* This could be |-ret|, but |ASN1_STRING_cmp| is not forbidden from
         * returning |INT_MIN|. */
        if (ret < 0) {
            return 1;
        } else if (ret > 0) {
            return -1;
        } else {
            return 0;
        }
    }

    return ret;
}

/* negate_twos_complement negates |len| bytes from |buf| in-place, interpreted
 * as a signed, big-endian two's complement value. */
static void negate_twos_complement(uint8_t *buf, size_t len)
{
    uint8_t borrow = 0;
    for (size_t i = len - 1; i < len; i--) {
        uint8_t t = buf[i];
        buf[i] = 0u - borrow - t;
        borrow |= t != 0;
    }
}

static int is_all_zeros(const uint8_t *in, size_t len) {
    for (size_t i = 0; i < len; i++) {
        if (in[i] != 0) {
            return 0;
        }
    }
    return 1;
}

int i2c_ASN1_INTEGER(const ASN1_INTEGER *in, unsigned char **outp)
{
    if (in == NULL) {
        return 0;
    }

    /* |ASN1_INTEGER|s should be represented minimally, but it is possible to
     * construct invalid ones. Skip leading zeros so this does not produce an
     * invalid encoding or break invariants. */
    int start = 0;
    while (start < in->length && in->data[start] == 0) {
        start++;
    }

    int is_negative = (in->type & V_ASN1_NEG) != 0;
    int pad;
    if (start >= in->length) {
        /* Zero is represented as a single byte. */
        is_negative = 0;
        pad = 1;
    } else if (is_negative) {
        /* 0x80...01 through 0xff...ff have a two's complement of 0x7f...ff
         * through 0x00...01 and need an extra byte to be negative.
         * 0x01...00 through 0x80...00 have a two's complement of 0xfe...ff
         * through 0x80...00 and can be negated as-is. */
        pad = in->data[start] > 0x80 ||
              (in->data[start] == 0x80 &&
               !is_all_zeros(in->data + start + 1, in->length - start - 1));
    } else {
        /* If the high bit is set, the signed representation needs an extra
         * byte to be positive. */
        pad = (in->data[start] & 0x80) != 0;
    }

    if (in->length - start > INT_MAX - pad) {
        OPENSSL_PUT_ERROR(ASN1, ERR_R_OVERFLOW);
        return 0;
    }
    int len = pad + in->length - start;
    assert(len > 0);
    if (outp == NULL) {
        return len;
    }

    if (pad) {
        (*outp)[0] = 0;
    }
    OPENSSL_memcpy(*outp + pad, in->data + start, in->length - start);
    if (is_negative) {
        negate_twos_complement(*outp, len);
        assert((*outp)[0] >= 0x80);
    } else {
        assert((*outp)[0] < 0x80);
    }
    *outp += len;
    return len;
}

ASN1_INTEGER *c2i_ASN1_INTEGER(ASN1_INTEGER **out, const unsigned char **inp,
                               long len)
{
    /*
     * This function can handle lengths up to INT_MAX - 1, but the rest of the
     * legacy ASN.1 code mixes integer types, so avoid exposing it to
     * ASN1_INTEGERS with larger lengths.
     */
    if (len < 0 || len > INT_MAX / 2) {
        OPENSSL_PUT_ERROR(ASN1, ASN1_R_TOO_LONG);
        return NULL;
    }

    CBS cbs;
    CBS_init(&cbs, *inp, (size_t)len);
    int is_negative;
    if (!CBS_is_valid_asn1_integer(&cbs, &is_negative)) {
        OPENSSL_PUT_ERROR(ASN1, ASN1_R_INVALID_INTEGER);
        return NULL;
    }

    ASN1_INTEGER *ret = NULL;
    if (out == NULL || *out == NULL) {
        ret = ASN1_INTEGER_new();
        if (ret == NULL) {
            return NULL;
        }
    } else {
        ret = *out;
    }

    /* Convert to |ASN1_INTEGER|'s sign-and-magnitude representation. First,
     * determine the size needed for a minimal result. */
    if (is_negative) {
        /* 0xff00...01 through 0xff7f..ff have a two's complement of 0x00ff...ff
         * through 0x000100...001 and need one leading zero removed. 0x8000...00
         * through 0xff00...00 have a two's complement of 0x8000...00 through
         * 0x0100...00 and will be minimally-encoded as-is. */
        if (CBS_len(&cbs) > 0 && CBS_data(&cbs)[0] == 0xff &&
            !is_all_zeros(CBS_data(&cbs) + 1, CBS_len(&cbs) - 1)) {
            CBS_skip(&cbs, 1);
        }
    } else {
        /* Remove the leading zero byte, if any. */
        if (CBS_len(&cbs) > 0 && CBS_data(&cbs)[0] == 0x00) {
            CBS_skip(&cbs, 1);
        }
    }

    if (!ASN1_STRING_set(ret, CBS_data(&cbs), CBS_len(&cbs))) {
        goto err;
    }

    if (is_negative) {
        ret->type = V_ASN1_NEG_INTEGER;
        negate_twos_complement(ret->data, ret->length);
    } else {
        ret->type = V_ASN1_INTEGER;
    }

    /* The value should be minimally-encoded. */
    assert(ret->length == 0 || ret->data[0] != 0);
    /* Zero is not negative. */
    assert(!is_negative || ret->length > 0);

    *inp += len;
    if (out != NULL) {
        *out = ret;
    }
    return ret;

 err:
    if (ret != NULL && (out == NULL || *out != ret)) {
        ASN1_INTEGER_free(ret);
    }
    return NULL;
}

int ASN1_INTEGER_set(ASN1_INTEGER *a, long v)
{
    if (v >= 0) {
        return ASN1_INTEGER_set_uint64(a, (uint64_t) v);
    }

    if (!ASN1_INTEGER_set_uint64(a, 0 - (uint64_t) v)) {
        return 0;
    }

    a->type = V_ASN1_NEG_INTEGER;
    return 1;
}

int ASN1_ENUMERATED_set(ASN1_ENUMERATED *a, long v)
{
    if (v >= 0) {
        return ASN1_ENUMERATED_set_uint64(a, (uint64_t) v);
    }

    if (!ASN1_ENUMERATED_set_uint64(a, 0 - (uint64_t) v)) {
        return 0;
    }

    a->type = V_ASN1_NEG_ENUMERATED;
    return 1;
}

static int asn1_string_set_uint64(ASN1_STRING *out, uint64_t v, int type)
{
    uint8_t *const newdata = OPENSSL_malloc(sizeof(uint64_t));
    if (newdata == NULL) {
        OPENSSL_PUT_ERROR(ASN1, ERR_R_MALLOC_FAILURE);
        return 0;
    }

    OPENSSL_free(out->data);
    out->data = newdata;
    v = CRYPTO_bswap8(v);
    memcpy(out->data, &v, sizeof(v));

    out->type = type;

    size_t leading_zeros;
    for (leading_zeros = 0; leading_zeros < sizeof(uint64_t); leading_zeros++) {
      if (out->data[leading_zeros] != 0) {
        break;
      }
    }

    out->length = sizeof(uint64_t) - leading_zeros;
    OPENSSL_memmove(out->data, out->data + leading_zeros, out->length);

    return 1;
}

int ASN1_INTEGER_set_uint64(ASN1_INTEGER *out, uint64_t v)
{
    return asn1_string_set_uint64(out, v, V_ASN1_INTEGER);
}

int ASN1_ENUMERATED_set_uint64(ASN1_ENUMERATED *out, uint64_t v)
{
    return asn1_string_set_uint64(out, v, V_ASN1_ENUMERATED);
}

static long asn1_string_get_long(const ASN1_STRING *a, int type)
{
    int neg = 0, i;

    if (a == NULL)
        return (0L);
    i = a->type;
    if (i == (type | V_ASN1_NEG))
        neg = 1;
    else if (i != type)
        return -1;

    OPENSSL_STATIC_ASSERT(sizeof(uint64_t) >= sizeof(long),
                          "long larger than uint64_t");

    if (a->length > (int)sizeof(uint64_t)) {
        /* hmm... a bit ugly, return all ones */
        return -1;
    }

    uint64_t r64 = 0;
    if (a->data != NULL) {
      for (i = 0; i < a->length; i++) {
          r64 <<= 8;
          r64 |= (unsigned char)a->data[i];
      }

      if (r64 > LONG_MAX) {
          return -1;
      }
    }

    long r = (long) r64;
    if (neg)
        r = -r;

    return r;
}

long ASN1_INTEGER_get(const ASN1_INTEGER *a)
{
    return asn1_string_get_long(a, V_ASN1_INTEGER);
}

long ASN1_ENUMERATED_get(const ASN1_ENUMERATED *a)
{
    return asn1_string_get_long(a, V_ASN1_ENUMERATED);
}

static ASN1_STRING *bn_to_asn1_string(const BIGNUM *bn, ASN1_STRING *ai,
                                      int type)
{
    ASN1_INTEGER *ret;
    if (ai == NULL) {
        ret = ASN1_STRING_type_new(type);
    } else {
        ret = ai;
    }
    if (ret == NULL) {
        OPENSSL_PUT_ERROR(ASN1, ASN1_R_NESTED_ASN1_ERROR);
        goto err;
    }

    if (BN_is_negative(bn) && !BN_is_zero(bn)) {
        ret->type = type | V_ASN1_NEG;
    } else {
        ret->type = type;
    }

    int len = BN_num_bytes(bn);
    if (!ASN1_STRING_set(ret, NULL, len) ||
        !BN_bn2bin_padded(ret->data, len, bn)) {
        goto err;
    }
    return ret;

 err:
    if (ret != ai) {
        ASN1_STRING_free(ret);
    }
    return NULL;
}

ASN1_INTEGER *BN_to_ASN1_INTEGER(const BIGNUM *bn, ASN1_INTEGER *ai)
{
    return bn_to_asn1_string(bn, ai, V_ASN1_INTEGER);
}

ASN1_ENUMERATED *BN_to_ASN1_ENUMERATED(const BIGNUM *bn, ASN1_ENUMERATED *ai)
{
    return bn_to_asn1_string(bn, ai, V_ASN1_ENUMERATED);
}

static BIGNUM *asn1_string_to_bn(const ASN1_STRING *ai, BIGNUM *bn, int type)
{
    if ((ai->type & ~V_ASN1_NEG) != type) {
        OPENSSL_PUT_ERROR(ASN1, ASN1_R_WRONG_INTEGER_TYPE);
        return NULL;
    }

    BIGNUM *ret;
    if ((ret = BN_bin2bn(ai->data, ai->length, bn)) == NULL)
        OPENSSL_PUT_ERROR(ASN1, ASN1_R_BN_LIB);
    else if (ai->type & V_ASN1_NEG)
        BN_set_negative(ret, 1);
    return (ret);
}

BIGNUM *ASN1_INTEGER_to_BN(const ASN1_INTEGER *ai, BIGNUM *bn)
{
    return asn1_string_to_bn(ai, bn, V_ASN1_INTEGER);
}

BIGNUM *ASN1_ENUMERATED_to_BN(const ASN1_ENUMERATED *ai, BIGNUM *bn)
{
    return asn1_string_to_bn(ai, bn, V_ASN1_ENUMERATED);
}
