#pragma once

#include <sys/ptrace.h>
#include <sys/wait.h>
#include <sys/personality.h>
#include <sys/user.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <fcntl.h>

#include <cxxabi.h>

#include "common.h"
#include "defer.h"
#include "Array.h"
#include "Hash_Table.h"

#include "dwarf/dwarf++.hh"
#include "elf/elf++.hh"

#include "debugger.h"
#include "declaration_parser.h"

// Applied in the context of dbg pointer
#define dbg_fail(message) {\
    if (dbg->verbose)  fprintf(stderr, "Error: %s\n", message);  \
    dbg->last_command_status = Command_Status::FAIL; \
  }
#define dbg_success() (dbg->last_command_status = Command_Status::SUCCESS)

// Debugger internal API
DBG_NAMESPACE_BEGIN

u64 offset_load_address(Debugger *dbg, u64 addr);
u64 offset_dwarf_address(Debugger *dbg, u64 addr);

dwarf::line_table::iterator get_line_entry_from_pc(Debugger *dbg, u64 pc);
dwarf::die get_function_from_pc(Debugger *dbg, u64 pc);

inline char * extract_file_name_from_path(char *file_path);

DBG_NAMESPACE_END
