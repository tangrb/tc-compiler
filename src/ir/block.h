/* TC 0.0.46 contract: basic blocks, explicit CFG and analysis unit (design §3.3). */
#ifndef TC_IR_BLOCK_H
#define TC_IR_BLOCK_H

#include <stddef.h>
#include <stdint.h>

#include "ir/instr.h"

typedef struct {
    uint32_t id;
    TcInstr *instrs;
    size_t n;
    uint32_t *succs; /* explicit CFG edges (standard §7) */
    uint32_t *preds;
    size_t nsucc;
    size_t npred;
} TcBlock;

typedef struct {
    uint32_t func_id; /* UINT32_MAX == top-level program */
    TcBlock *blocks;
    size_t nblocks;
    uint32_t entry;
    const TcType **vreg_types;
    size_t nvregs;
    uint32_t *vreg_objects; /* vreg -> TcObject id */
    size_t nvreg_objects;
} TcUnit;

#define TC_UNIT_TOP_LEVEL UINT32_MAX

#endif /* TC_IR_BLOCK_H */
