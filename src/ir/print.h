/* TC 0.0.46 IR textual printing (design §3, §14 `ir/print`). */
#ifndef TC_IR_PRINT_H
#define TC_IR_PRINT_H

#include <stdio.h>

#include "ir/ir.h"

/* Write the deterministic textual dump of `ir` to `out`.  Returns 0 on
 * success, -1 for a NULL argument. */
int tc_ir_print(const TcIR *ir, FILE *out);
const char *tc_ir_op_name(TcOpcode op);
const char *tc_ir_instr_name(const TcInstr *instr, char *buffer, size_t capacity);

#endif /* TC_IR_PRINT_H */
