/*
 * tc_embed_slots.h — TC-Embed 槽位数组容量口径（VM / AOT / 宿主共享）
 *
 * 槽位数组由「声明槽位 + 临时槽位区」两部分组成：声明槽位是程序自身的运行期
 * 槽位（索引 [0, 声明槽位数)）；临时槽位区供宿主平铺 C 侧数据（如
 * `tc_embed_make_ptr`），位于声明槽位**之上**（索引 [声明槽位数, 容量)），
 * 从容量顶端向下栈式分配，与声明槽位互不重叠。
 *
 * 容量 = 声明槽位数 + max(声明槽位数, TC_EMBED_TMP_MIN)。
 * AOT 生成的全局 `slots[]` 数组与 VM 模式的槽位缓冲都按本容量定长。
 */
#ifndef TC_EMBED_SLOTS_H
#define TC_EMBED_SLOTS_H

#include <stddef.h>

/** 临时槽位区最小保留量（声明槽位很少时的下限） */
#define TC_EMBED_TMP_MIN 16

/** 槽位数组总容量（声明槽位 + 临时区）；declared_slots = 0 时按 1 个声明槽位计 */
static inline size_t tc_embed_slot_capacity_of(size_t declared_slots) {
    size_t base = declared_slots > 0 ? declared_slots : 1;
    size_t reserve = base > TC_EMBED_TMP_MIN ? base : TC_EMBED_TMP_MIN;

    return base + reserve;
}

#endif /* TC_EMBED_SLOTS_H */
