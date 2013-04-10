/* -*- Mode: C; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */

#include "keys.h"
#include "../bitfield.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <snappy-c.h>


#define dec_uint16(b) (decode_raw16(*((raw_16 *) b)))

static void enc_uint16(uint16_t u, char **buf);


LIBCOUCHSTORE_API
couchstore_error_t decode_view_btree_key(const char *bytes, size_t len,
                                        view_btree_key_t **key)
{
    view_btree_key_t *k = NULL;
    uint16_t sz;

    k = (view_btree_key_t *) malloc(sizeof(view_btree_key_t));
    if (k == NULL) {
        goto alloc_error;
    }

    k->json_key.buf = NULL;

    sz = dec_uint16(bytes);

    bytes += 2;
    len -= 2;

    k->json_key.size = sz;
    k->json_key.buf = (char *) malloc(sz);

    if (k->json_key.buf == NULL) {
        goto alloc_error;
    }

    memcpy(k->json_key.buf, bytes, sz);
    bytes += sz;

    len -= sz;

    k->doc_id.size = len;

    k->doc_id.buf = (char *) malloc(len);


    if (k->doc_id.buf == NULL) {
        goto alloc_error;
    }

    memcpy(k->doc_id.buf, bytes, len);

    *key = k;

    return COUCHSTORE_SUCCESS;

 alloc_error:
    free_view_btree_key(k);
    return COUCHSTORE_ERROR_ALLOC_FAIL;
}


LIBCOUCHSTORE_API
couchstore_error_t encode_view_btree_key(const view_btree_key_t *key,
                                       char **buffer,
                                       size_t *buffer_size)
{
    char *buf = NULL, *b = NULL;
    size_t sz = 0;

    sz += 2;             /* size_t */
    sz += key->json_key.size;
    sz += key->doc_id.size;

    b = buf = (char *) malloc(sz);
    if (buf == NULL) {
        goto alloc_error;
    }

    enc_uint16(key->json_key.size, &b);

    memcpy(b, key->json_key.buf, key->json_key.size);
    b += key->json_key.size;

    memcpy(b, key->doc_id.buf, key->doc_id.size);

    *buffer = buf;
    *buffer_size = sz;

    return COUCHSTORE_SUCCESS;

 alloc_error:
    free(buf);
    *buffer = NULL;
    *buffer_size = 0;
    return COUCHSTORE_ERROR_ALLOC_FAIL;
}


LIBCOUCHSTORE_API
void free_view_btree_key(view_btree_key_t *key)
{
    if (key == NULL) {
        return;
    }

    free(key->json_key.buf);
    free(key->doc_id.buf);
    free(key);
}

LIBCOUCHSTORE_API
couchstore_error_t decode_view_id_btree_key(const char *bytes, size_t len,
                                             view_id_btree_key_t **key)
{
    view_id_btree_key_t *k = NULL;

    k = (view_id_btree_key_t *) malloc(sizeof(view_id_btree_key_t));
    if (k == NULL) {
        goto alloc_error;
    }

    k->doc_id.buf = NULL;

    k->partition = dec_uint16(bytes);
    bytes += 2;
    len -= 2;

    k->doc_id.size = len;

    k->doc_id.buf = (char *) malloc(len);

    if (k->doc_id.buf == NULL) {
        goto alloc_error;
    }

    memcpy(k->doc_id.buf, bytes, len);

    *key = k;

    return COUCHSTORE_SUCCESS;

 alloc_error:
    free_view_id_btree_key(k);
    return COUCHSTORE_ERROR_ALLOC_FAIL;
}


LIBCOUCHSTORE_API
couchstore_error_t encode_view_id_btree_key(const view_id_btree_key_t *key,
                                            char **buffer, size_t *buffer_size)
{
    char *buf = NULL, *b = NULL;
    size_t sz = 0;


    sz += 2;             /* uint16_t */

    sz += key->doc_id.size;


    b = buf = (char *) malloc(sz);
    if (buf == NULL) {
        goto alloc_error;
    }

    enc_uint16(key->partition, &b);


    memcpy(b, key->doc_id.buf, key->doc_id.size);
    b += key->doc_id.size;


    *buffer = buf;
    *buffer_size = sz;

    return COUCHSTORE_SUCCESS;

 alloc_error:
    free(buf);
    *buffer = NULL;
    *buffer_size = 0;
    return COUCHSTORE_ERROR_ALLOC_FAIL;
}


LIBCOUCHSTORE_API
void free_view_id_btree_key(view_id_btree_key_t *key)
{
    if (key == NULL) {
        return;
    }

    free(key->doc_id.buf);
    free(key);
}

static void enc_uint16(uint16_t u, char **buf)
{
    raw_16 k = encode_raw16(u);
    memcpy(*buf, &k, 2);
    *buf += 2;
}

