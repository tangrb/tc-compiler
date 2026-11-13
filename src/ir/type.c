/* TC 0.0.46 contract: type table implementation (design §2.2). */
#include "ir/type.h"

#include <string.h>

#include "runtime/alloc.h"

struct TcTypeTable {
    const TcAllocator *alloc;
    TcType *scalars[TC_TYPE_SCALAR_COUNT];
    TcType **nodes; /* every interned node, for destruction */
    size_t nnodes;
    size_t capnodes;
    int frozen;
};

static TcType *type_alloc(TcTypeTable *table, TcTypeTag tag) {
    TcType *t = (TcType *)table->alloc->alloc(table->alloc->ctx, sizeof(TcType));
    if (t == NULL) {
        return NULL;
    }
    memset(t, 0, sizeof(*t));
    t->tag = tag;
    return t;
}

static int table_reserve(TcTypeTable *table, size_t extra) {
    size_t need = table->nnodes + extra;
    if (need <= table->capnodes) {
        return 1;
    }
    {
        size_t cap = table->capnodes ? table->capnodes * 2 : 16;
        TcType **nodes;
        while (cap < need) {
            cap *= 2;
        }
        nodes = (TcType **)table->alloc->realloc(table->alloc->ctx, table->nodes,
                                                 cap * sizeof(TcType *));
        if (nodes == NULL) {
            return 0;
        }
        table->nodes = nodes;
        table->capnodes = cap;
    }
    return 1;
}

TcTypeTable *tc_type_table_create(const TcAllocator *alloc) {
    TcTypeTable *table;
    if (alloc == NULL) {
        alloc = tc_allocator_libc();
    }
    if (alloc->alloc == NULL || alloc->realloc == NULL || alloc->free == NULL) {
        return NULL;
    }
    table = (TcTypeTable *)alloc->alloc(alloc->ctx, sizeof(TcTypeTable));
    if (table == NULL) {
        return NULL;
    }
    memset(table, 0, sizeof(*table));
    table->alloc = alloc;
    /* Materialize every scalar singleton before Analyze can freeze the table.
     * A frozen table must never create a node on a later scalar lookup. */
    {
        size_t i;
        for (i = 0; i < TC_TYPE_SCALAR_COUNT; i++) {
            table->scalars[i] = type_alloc(table, (TcTypeTag)i);
            if (table->scalars[i] == NULL) {
                tc_type_table_destroy(table);
                return NULL;
            }
        }
    }
    return table;
}

void tc_type_table_destroy(TcTypeTable *table) {
    size_t i;
    if (table == NULL) {
        return;
    }
    for (i = 0; i < table->nnodes; i++) {
        TcType *node = table->nodes[i];
        if (node->tag == TC_TY_FNPTR && node->fnptr.params != NULL) {
            table->alloc->free(table->alloc->ctx, (void *)node->fnptr.params);
        }
        table->alloc->free(table->alloc->ctx, node);
    }
    for (i = 0; i < TC_TYPE_SCALAR_COUNT; i++) {
        if (table->scalars[i] != NULL) {
            table->alloc->free(table->alloc->ctx, table->scalars[i]);
        }
    }
    if (table->nodes != NULL) {
        table->alloc->free(table->alloc->ctx, table->nodes);
    }
    table->alloc->free(table->alloc->ctx, table);
}

const TcType *tc_type_scalar(TcTypeTable *table, TcTypeTag tag) {
    if (table == NULL || (unsigned)tag >= TC_TYPE_SCALAR_COUNT) {
        return NULL;
    }
    /* Scalars are pre-created by tc_type_table_create(); freeze never adds a
     * node.  If a malformed table is observed, fail closed instead of mutating
     * a frozen table. */
    return table->scalars[tag];
}

static int intern_allowed(const TcTypeTable *table) {
    return table != NULL && !table->frozen;
}

static const TcType *register_node(TcTypeTable *table, TcType *node) {
    if (!table_reserve(table, 1)) {
        table->alloc->free(table->alloc->ctx, node);
        return NULL;
    }
    table->nodes[table->nnodes++] = node;
    return node;
}

const TcType *tc_type_ptr(TcTypeTable *table, const TcType *pointee) {
    TcType *node;
    size_t i;
    if (!intern_allowed(table) || pointee == NULL || pointee->tag == TC_TY_VOID) {
        return NULL;
    }
    for (i = 0; i < table->nnodes; i++) {
        const TcType *candidate = table->nodes[i];
        if (candidate->tag == TC_TY_PTR && candidate->ptr.pointee == pointee) {
            return candidate;
        }
    }
    node = type_alloc(table, TC_TY_PTR);
    if (node == NULL) {
        return NULL;
    }
    node->ptr.pointee = pointee;
    return register_node(table, node);
}

const TcType *tc_type_fnptr(TcTypeTable *table, const TcType *ret,
                            const TcType *const *params, size_t nparams) {
    TcType *node;
    size_t i;
    const TcType **copy = NULL;
    if (!intern_allowed(table) || ret == NULL) {
        return NULL;
    }
    if (ret->tag == TC_TY_VOID) {
        /* void return is allowed for fnptr; no value check needed. */
    }
    if (nparams > 0 && params == NULL) {
        return NULL;
    }
    for (i = 0; i < nparams; i++) {
        if (params[i] == NULL || params[i]->tag == TC_TY_VOID) {
            return NULL;
        }
    }
    for (i = 0; i < table->nnodes; i++) {
        const TcType *candidate = table->nodes[i];
        size_t j;
        if (candidate->tag != TC_TY_FNPTR) {
            continue;
        }
        if (candidate->fnptr.ret != ret || candidate->fnptr.n != nparams) {
            continue;
        }
        for (j = 0; j < nparams; j++) {
            if (candidate->fnptr.params[j] != params[j]) {
                break;
            }
        }
        if (j == nparams) {
            return candidate;
        }
    }
    if (nparams > 0) {
        copy = (const TcType **)table->alloc->alloc(table->alloc->ctx,
                                                   nparams * sizeof(*copy));
        if (copy == NULL) {
            return NULL;
        }
        for (i = 0; i < nparams; i++) {
            copy[i] = params[i];
        }
    }
    node = type_alloc(table, TC_TY_FNPTR);
    if (node == NULL) {
        if (copy != NULL) {
            table->alloc->free(table->alloc->ctx, copy);
        }
        return NULL;
    }
    node->fnptr.ret = ret;
    node->fnptr.params = copy;
    node->fnptr.n = nparams;
    {
        const TcType *registered = register_node(table, node);
        if (registered == NULL && copy != NULL) {
            table->alloc->free(table->alloc->ctx, copy);
        }
        return registered;
    }
}

const TcType *tc_type_struct(TcTypeTable *table, uint32_t struct_id) {
    TcType *node;
    size_t i;
    if (!intern_allowed(table)) {
        return NULL;
    }
    for (i = 0; i < table->nnodes; i++) {
        const TcType *candidate = table->nodes[i];
        if (candidate->tag == TC_TY_STRUCT && candidate->struct_.struct_id == struct_id) {
            return candidate;
        }
    }
    node = type_alloc(table, TC_TY_STRUCT);
    if (node == NULL) {
        return NULL;
    }
    node->struct_.struct_id = struct_id;
    return register_node(table, node);
}

int tc_type_table_freeze(TcTypeTable *table) {
    if (table == NULL) {
        return 0;
    }
    table->frozen = 1;
    return 1;
}

int tc_type_table_is_frozen(const TcTypeTable *table) {
    return table != NULL && table->frozen;
}

int tc_type_equal(const TcType *a, const TcType *b) {
    return a == b;
}

int tc_type_is_value(const TcType *type) {
    return type != NULL && type->tag != TC_TY_VOID;
}

int tc_type_is_integer(const TcType *type) {
    if (type == NULL) {
        return 0;
    }
    switch (type->tag) {
    case TC_TY_I8:
    case TC_TY_I16:
    case TC_TY_I32:
    case TC_TY_I64:
    case TC_TY_U8:
    case TC_TY_U16:
    case TC_TY_U32:
    case TC_TY_U64:
    case TC_TY_ISIZE:
    case TC_TY_USIZE:
        return 1;
    default:
        return 0;
    }
}

int tc_type_is_signed_integer(const TcType *type) {
    if (type == NULL) {
        return 0;
    }
    switch (type->tag) {
    case TC_TY_I8:
    case TC_TY_I16:
    case TC_TY_I32:
    case TC_TY_I64:
    case TC_TY_ISIZE:
        return 1;
    default:
        return 0;
    }
}

int tc_type_is_float(const TcType *type) {
    return type != NULL && (type->tag == TC_TY_F32 || type->tag == TC_TY_F64);
}

int tc_type_is_scalar(const TcType *type) {
    return type != NULL && type->tag < TC_TYPE_SCALAR_COUNT;
}

unsigned tc_type_bits(const TcType *type, unsigned addr_width) {
    if (type == NULL || (addr_width != 32 && addr_width != 64)) {
        return 0;
    }
    switch (type->tag) {
    case TC_TY_I8:
    case TC_TY_U8:
    case TC_TY_BOOL:
        return 8;
    case TC_TY_I16:
    case TC_TY_U16:
        return 16;
    case TC_TY_I32:
    case TC_TY_U32:
    case TC_TY_F32:
        return 32;
    case TC_TY_I64:
    case TC_TY_U64:
    case TC_TY_F64:
        return 64;
    case TC_TY_ISIZE:
    case TC_TY_USIZE:
    case TC_TY_PTR:
    case TC_TY_FNPTR:
        return addr_width;
    case TC_TY_VOID:
    case TC_TY_STRUCT:
    default:
        return 0;
    }
}
