/* -*- Mode: C; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
#include "config.h"

#include "arena.h"
#include "bitfield.h"
#include "couch_btree.h"
#include "internal.h"
#include "mergesort.h"
#include "reduces.h"
#include "tree_writer.h"
#include "util.h"

#include <stdlib.h>


#define ID_SORT_CHUNK_SIZE (100 * 1024 * 1024) // 100MB. Make tuneable?
#define ID_SORT_MAX_RECORD_SIZE 4196


static int read_id_record(FILE *in, void *buf, void *ctx);
static int write_id_record(FILE *out, void *ptr, void *ctx);
static int compare_id_record(void* r1, void* r2, void *ctx);


struct TreeWriter {
    FILE* file;
    compare_callback key_compare;
};


couchstore_error_t TreeWriterOpen(const char* unsortedFilePath,
                                  compare_callback key_compare,
                                  TreeWriter** out_writer)
{
    couchstore_error_t errcode = COUCHSTORE_SUCCESS;
    TreeWriter* writer = calloc(1, sizeof(TreeWriter));
    error_unless(writer, COUCHSTORE_ERROR_ALLOC_FAIL);
    writer->file = unsortedFilePath ? fopen(unsortedFilePath, "r+b") : tmpfile();
    if (!writer->file) {
        TreeWriterFree(writer);
        error_pass(COUCHSTORE_ERROR_NO_SUCH_FILE);
    }
    if (unsortedFilePath) {
        fseek(writer->file, 0, SEEK_END);  // in case more items will be added
    }
    writer->key_compare = (key_compare ? key_compare : ebin_cmp);
    *out_writer = writer;
cleanup:
    return errcode;
}


void TreeWriterFree(TreeWriter* writer)
{
    if (writer && writer->file) {
        fclose(writer->file);
    }
    free(writer);
}


couchstore_error_t TreeWriterAddItem(TreeWriter* writer, sized_buf key, sized_buf value)
{
    couchstore_error_t errcode = COUCHSTORE_SUCCESS;

    uint16_t klen = (uint16_t) key.size;
    uint32_t vlen = (uint32_t) value.size;
    error_unless(fwrite(&klen, sizeof(klen), 1, writer->file) == 1, COUCHSTORE_ERROR_WRITE);
    error_unless(fwrite(&vlen, sizeof(vlen), 1, writer->file) == 1, COUCHSTORE_ERROR_WRITE);
    error_unless(fwrite(key.buf, key.size, 1, writer->file) == 1, COUCHSTORE_ERROR_WRITE);
    error_unless(fwrite(value.buf, value.size, 1, writer->file) == 1, COUCHSTORE_ERROR_WRITE);

cleanup:
    return errcode;
}


couchstore_error_t TreeWriterSort(TreeWriter* writer)
{
    rewind(writer->file);
    return merge_sort(writer->file, writer->file,
                      read_id_record, write_id_record, compare_id_record,
                      writer,  // 'context' parameter to the above callbacks
                      ID_SORT_MAX_RECORD_SIZE, ID_SORT_CHUNK_SIZE, NULL);
}


couchstore_error_t TreeWriterWrite(TreeWriter* writer, Db* target)
{
    couchstore_error_t errcode = COUCHSTORE_SUCCESS;
    arena* transient_arena = new_arena(0);
    arena* persistent_arena = new_arena(0);
    error_unless(transient_arena && persistent_arena, COUCHSTORE_ERROR_ALLOC_FAIL);

    rewind(writer->file);

    // Create the structure to write the tree to the db:
    compare_info idcmp;
    sized_buf tmp;
    idcmp.compare = writer->key_compare;
    idcmp.arg = &tmp;

    couchfile_modify_result* target_mr = new_btree_modres(persistent_arena,
                                                          transient_arena,
                                                          target, &idcmp,
                                                          by_id_reduce, by_id_rereduce);
    if(target_mr == NULL) {
        error_pass(COUCHSTORE_ERROR_ALLOC_FAIL);
    }

    // Read all the key/value pairs from the file and add them to the tree:
    uint16_t klen;
    uint32_t vlen;
    sized_buf k, v;
    while(1) {
        if(fread(&klen, sizeof(klen), 1, writer->file) != 1) {
            break;
        }
        if(fread(&vlen, sizeof(vlen), 1, writer->file) != 1) {
            break;
        }
        k.size = klen;
        k.buf = arena_alloc(transient_arena, klen);
        v.size = vlen;
        v.buf = arena_alloc(transient_arena, vlen);
        if(fread(k.buf, klen, 1, writer->file) != 1) {
            error_pass(COUCHSTORE_ERROR_READ);
        }
        if(fread(v.buf, vlen, 1, writer->file) != 1) {
            error_pass(COUCHSTORE_ERROR_READ);
        }
        //printf("K: '%.*s'\n", klen, k.buf);
        mr_push_item(&k, &v, target_mr);
        if(target_mr->count == 0) {
            /* No items queued, we must have just flushed. We can safely rewind the transient arena. */
            arena_free_all(transient_arena);
        }
    }

    // Check for file error:
    int readerr = ferror(writer->file);
    if(readerr != 0 && readerr != EOF) {
        error_pass(COUCHSTORE_ERROR_READ);
    }

    // Finish up the tree:
    target->header.by_id_root = complete_new_btree(target_mr, &errcode);

cleanup:
    delete_arena(transient_arena);
    delete_arena(persistent_arena);
    return errcode;
}


//////// MERGE-SORT CALLBACKS:


typedef struct extsort_record {
    sized_buf k;
    sized_buf v;
    char buf[1];
} extsort_record;

static int read_id_record(FILE *in, void *buf, void *ctx)
{
    (void) ctx;
    uint16_t klen;
    uint32_t vlen;
    extsort_record *rec = (extsort_record *) buf;
    if(fread(&klen, 2, 1, in) != 1) {
        return 0;
    }
    if(fread(&vlen, 4, 1, in) != 1) {
        return 0;
    }
    rec->k.size = klen;
    rec->k.buf = rec->buf;
    rec->v.size = vlen;
    rec->v.buf = rec->buf + klen;
    if(fread(rec->k.buf, klen, 1, in) != 1) {
        return 0;
    }
    if(fread(rec->v.buf, vlen, 1, in) != 1) {
        return 0;
    }
    return sizeof(extsort_record) + klen + vlen;
}

static int write_id_record(FILE *out, void *ptr, void *ctx)
{
    (void) ctx;
    extsort_record *rec = (extsort_record *) ptr;
    uint16_t klen = (uint16_t) rec->k.size;
    uint32_t vlen = (uint32_t) rec->v.size;
    if(fwrite(&klen, 2, 1, out) != 1) {
        return 0;
    }
    if(fwrite(&vlen, 4, 1, out) != 1) {
        return 0;
    }
    if(fwrite(rec->buf, vlen + klen, 1, out) != 1) {
        return 0;
    }
    return 1;
}

static int compare_id_record(void* r1, void* r2, void *ctx)
{
    TreeWriter* writer = ctx;
    extsort_record *e1 = (extsort_record *) r1, *e2 = (extsort_record *) r2;
    e1->k.buf = e1->buf;
    e2->k.buf = e2->buf;
    return writer->key_compare(&e1->k, &e2->k);
}
