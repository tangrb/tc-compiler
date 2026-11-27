#ifndef TC_VM_GDB_JOURNAL_H
#define TC_VM_GDB_JOURNAL_H

#include <stddef.h>
#include <stdint.h>

#include "diagnostic/diagnostic.h"

typedef struct TcGdbJournal TcGdbJournal;

TcImplError tc_gdb_journal_open(TcGdbJournal **out, const char *directory);
void tc_gdb_journal_close(TcGdbJournal *journal);
int tc_gdb_journal_write(TcGdbJournal *journal, const uint8_t *bytes, size_t size);
uint64_t tc_gdb_journal_high_water(const TcGdbJournal *journal);
int tc_gdb_journal_failed(const TcGdbJournal *journal);

#endif /* TC_VM_GDB_JOURNAL_H */
