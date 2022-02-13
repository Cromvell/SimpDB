#include "debugger_internal.h"

#include "Hash_Table.cpp"
#include "declaration_parser.cpp"
#include "breakpoint.cpp"

// Plan: Write out debugging lib, which can be used in ImGui graphical program
// * Lib API
//   * Set SUCCESS and fail flags
//   * Introduce debugger states 
//   * Add a verbose flag for some functions :VerboseFlag
// * Remove exception throws
// * Add conditional breakpoints
// * Figure out how to deal with multiple breakpoints on one line. Currently setting up new breakpoint leak an old one.
// * process_vm_readv and process_vm_writev
// * ImGUI

DBG_NAMESPACE_BEGIN

void init(Debugger * dbg) {
  if (dbg) {
    dbg->breakpoints.init();
    dbg->breakpoint_map.init();

    dbg->last_operation_state = dbg::Command_Status::NO_STATUS;
  }
}

void deinit(Debugger * dbg) {
  if (dbg) {
    if (dbg->executable_path)  free(dbg->executable_path);
    if (dbg->argument_string)  free(dbg->argument_string);

    dbg->breakpoints.deinit();
    dbg->breakpoint_map.deinit();
  }
}

////////////////////////////////////////////
//
//  Registers
//


constexpr u32 registers_count = 27;

struct Register_Descriptor {
  Register r;
  s32 dwarf_r;
  char *name;
};

const Register_Descriptor global_register_descriptors[registers_count] = {
  { Register::r15,      15, (char *)"r15"      },
  { Register::r14,      14, (char *)"r14"      },
  { Register::r13,      13, (char *)"r13"      },
  { Register::r12,      12, (char *)"r12"      },
  { Register::rbp,       6, (char *)"rbp"      },
  { Register::rbx,       3, (char *)"rbx"      },
  { Register::r11,      11, (char *)"r11"      },
  { Register::r10,      10, (char *)"r10"      },
  { Register::r9,        9, (char *)"r9"       },
  { Register::r8,        8, (char *)"r8"       },
  { Register::rax,       0, (char *)"rax"      },
  { Register::rcx,       2, (char *)"rcx"      },
  { Register::rdx,       1, (char *)"rdx"      },
  { Register::rsi,       4, (char *)"rsi"      },
  { Register::rdi,       5, (char *)"rdi"      },
  { Register::orig_rax, -1, (char *)"orig_rax" },
  { Register::rip,      -1, (char *)"rip"      },
  { Register::cs,       51, (char *)"cs"       },
  { Register::eflags,   49, (char *)"eflags"   },
  { Register::rsp,       7, (char *)"rsp"      },
  { Register::ss,       52, (char *)"ss"       },
  { Register::fs_base,  58, (char *)"fs_base"  },
  { Register::gs_base,  59, (char *)"gs_base"  },
  { Register::ds,       53, (char *)"ds"       },
  { Register::es,       50, (char *)"es"       },
  { Register::fs,       54, (char *)"fs"       },
  { Register::gs,       55, (char *)"gs"       },
};

u64 read_register(pid_t pid, Register r) {
  if (r == Register::UNKNOWN)  assert(false && "Attempt to read from UNKNOWN register");

  user_regs_struct regs;
  // @Hack: We're get to kernel reading each register. Maybe it needs some caching or
  //        an option to read batch of registers with one ptrace call
  ptrace(PTRACE_GETREGS, pid, nullptr, &regs);

  return *(reinterpret_cast<u64 *> (&regs) + (u64)r);
}

u64 pid_read_register(pid_t pid, Register r) {
  return read_register(pid, r);
}

u64 read_register(Debugger *dbg, Register r) {
  return read_register(dbg->debugee_pid, r);
}

void write_register(pid_t pid, Register r, u64 value) {
  if (r == Register::UNKNOWN)  assert(false && "Attempt to write to UNKNOWN register");

  user_regs_struct regs;
  // @Hack: We're get to kernel reading each register. Maybe it needs some caching or
  //        an option to read batch of registers with one ptrace call
  ptrace(PTRACE_GETREGS, pid, nullptr, &regs);

  *(reinterpret_cast<u64 *> (&regs) + (u64)r) = value;

  ptrace(PTRACE_SETREGS, pid, nullptr, &regs);
}

void write_register(Debugger *dbg, Register r, u64 value) {
  write_register(dbg->debugee_pid, r, value);
}

inline u64 get_pc(Debugger *dbg) {
  return read_register(dbg, Register::rip);
}

inline void set_pc(Debugger *dbg, u64 pc) {
  write_register(dbg, Register::rip, pc);
}

u64 read_dwarf_register(pid_t pid, u32 register_number) {
  const Register_Descriptor *match = nullptr;
  For_Count (registers_count, i) {
    if (global_register_descriptors[i].dwarf_r == register_number)  match = &(global_register_descriptors[i]);
  }

  if (match == nullptr) {
    fprintf(stderr, "ERROR: Unknown dwarf register\n");
  }

  return pid_read_register(pid, match->r);
}

u64 read_dwarf_register(Debugger *dbg, u32 register_number) {
  return read_dwarf_register(dbg->debugee_pid, register_number);
}

//
//  Registers
//
///////////////////////////////////////////////////

// TODO: Currently moved data goes through kernel space.
//       Task: rewrite this to process_vm_readv and process_vm_writev (https://man7.org/linux/man-pages/man2/process_vm_readv.2.html)
//          OR use /proc/<pid>/mem instead ptrace (compare options' performance and portability to other systems)
u64 read_memory(Debugger *dbg, u64 address) {
  return ptrace(PTRACE_PEEKDATA, dbg->debugee_pid, address, nullptr);
}

void write_memory(Debugger *dbg, u64 address, u64 value) {
  ptrace(PTRACE_POKEDATA, dbg->debugee_pid, address, value);
}

siginfo_t get_signal_info(Debugger *dbg) {
  siginfo_t info;
  ptrace(PTRACE_GETSIGINFO, dbg->debugee_pid, nullptr, &info);
  return info;
}

u64 offset_load_address(Debugger *dbg, u64 addr) {
  return addr - dbg->load_address;
}

u64 offset_dwarf_address(Debugger *dbg, u64 addr) {
  return addr + dbg->load_address;
}

u64 get_offset_pc(Debugger *dbg) {
  return offset_load_address(dbg, get_pc(dbg));
}

void handle_sigtrap(Debugger *dbg, siginfo_t info) {
  switch (info.si_code) {
  case SI_KERNEL:
  case TRAP_BRKPT: {
    set_pc(dbg, get_pc(dbg) - 1);
    auto current_pc = get_pc(dbg);
    printf("Hit breakpoint at adress 0x%lx\n", current_pc);
    auto offset_pc = offset_load_address(dbg, current_pc);
    // :VerboseFlag
    // auto line_entry = get_line_entry_from_pc(dbg, offset_pc);
    // print_source(dbg, line_entry->file->path.c_str(), line_entry->line);
    return;
  }
  case TRAP_TRACE: // single stepping trap
    return;
  case SI_USER:
    printf("Got SI_USER!!!!\n");
    break;
  default:
    printf("Unknown SIGTRAP code: %d\n", info.si_code);
    return;
  }
}

void initialize_load_address(Debugger * dbg) {
  // If dynamic library was loaded
  if (dbg->elf.get_hdr().type == elf::et::dyn) {
    char proc_map_path[64];
    sprintf(proc_map_path, "/proc/%d/maps", dbg->debugee_pid);

    FILE * fp = fopen(proc_map_path, "r");
    FILE * fp_get_size = fopen(proc_map_path, "r");
    defer { fclose(fp); fclose(fp_get_size); };

    u64 start_addr, end_addr;
    auto executable_path_len = strlen(dbg->executable_path);
    char *file_path = nullptr;
    while (!feof(fp)) {
      u32 path_length = 0;
      fscanf(fp_get_size, "%*s %*s %*s %*s %*s %*s%n\n", &path_length);

      file_path = static_cast <char *>(malloc(path_length + 1)); // @Speed: Meh... write something better without mallocing within a loop
      fscanf(fp, "%lx-%lx %*s %*s %*s %*s %s\n", &start_addr, &end_addr, file_path);

      if (strncmp(file_path, dbg->executable_path, executable_path_len) == 0)  break;

      free(file_path);
      file_path = nullptr;
    }

    if (!file_path) {
      printf("Couldn't find load address of file: %s\n", dbg->executable_path);
      assert(false);
    }

    dbg->load_address = start_addr;
  }
}

////////////////////////////////
//
//  Debug info processing
//

void load_debug_info(Debugger *dbg) {
  auto fd = open(dbg->executable_path, O_RDONLY);

  dbg->elf   = elf::elf(elf::create_mmap_loader(fd));
  dbg->dwarf = dwarf::dwarf(dwarf::elf::create_loader(dbg->elf));
}

dwarf::die get_function_from_pc(Debugger *dbg, u64 pc) {
  for (auto &cu : dbg->dwarf.compilation_units()) {
    if (dwarf::die_pc_range(cu.root()).contains(pc)) {
      for (const auto &die : cu.root()) {
        if (die.tag == dwarf::DW_TAG::subprogram) {
          if (die.has(dwarf::DW_AT::low_pc) && dwarf::die_pc_range(die).contains(pc)) {
            return die;
          }
        }
      }
    }
  }

  throw std::out_of_range("Cannot find function"); // TODO: Get rid of exception throws
}

dwarf::line_table::iterator get_line_entry_from_pc(Debugger *dbg, u64 pc) {
  for (auto &cu : dbg->dwarf.compilation_units()) {
    if (dwarf::die_pc_range(cu.root()).contains(pc)) {
      auto &lt = cu.get_line_table();
      auto it = lt.find_address(pc); // @Bug: libelfin unable to find end line of template function for some reason
      if (it == lt.end()) {
        throw std::out_of_range("Cannot find line entry"); // TODO: Get rid of exception throws
      } else {
        return it;
      }
    }
  }

  throw std::out_of_range("Cannot find line entry"); // TODO: Get rid of exception throws
}

void wait_for_signal(Debugger * dbg) {
  s32 wait_status;
  s32 options = 0;
  waitpid(dbg->debugee_pid, &wait_status, options);

  auto siginfo = get_signal_info(dbg);

  switch (siginfo.si_signo) {
  case SIGTRAP:
    handle_sigtrap(dbg, siginfo);
    break;
  case SIGSEGV:
    printf("SEGFAULT! Reason: %d\n", siginfo.si_code);
    break;
  default:
    printf("Got signal %s\n", strsignal(siginfo.si_signo));
    break;
  }
}


// TODO: Handle arguments
void debug(Debugger * dbg, const char * executable_path, const char * arguments) {
  auto pid = fork();

  if (pid == 0) {
    personality(ADDR_NO_RANDOMIZE);

    ptrace(PTRACE_TRACEME, 0, nullptr, nullptr);
    execl(executable_path, executable_path, nullptr);
  } else if (pid >= 1) {
    dbg->debugee_pid = pid;

    // @Note: realpath here resolves to the parent process aka tracer
    // char exe_link[64];
    // sprintf(exe_link, "/proc/%d/exe", dbg->debugee_pid);
    // dbg->executable_path = realpath(exe_link, nullptr);

    // @Note: but this one pointing to original file
    dbg->executable_path = realpath(executable_path, nullptr);

    if (arguments) {
      dbg->argument_string = strdup(arguments);
    }

    load_debug_info(dbg);
    wait_for_signal(dbg);
    initialize_load_address(dbg);
  }
}

void attach(Debugger * dbg, u32 pid) {
  dbg->debugee_pid = pid;
  ptrace(PTRACE_ATTACH, pid, nullptr, nullptr);

  char exe_link[64];
  sprintf(exe_link, "/proc/%d/exe", pid);

  dbg->executable_path = realpath(exe_link, nullptr);

  load_debug_info(dbg);
  wait_for_signal(dbg);
  initialize_load_address(dbg);
}

inline char * extract_file_name_from_path(char *file_path) {
  char *file_name = file_path + strlen(file_path) - 1;
  while (*(file_name - 1) != '/')  file_name--;
  return file_name;
}

Source_Location get_source_location(Debugger *dbg) {
  auto current_pc = get_pc(dbg);
  auto offset_pc = offset_load_address(dbg, current_pc);
  auto line_entry = get_line_entry_from_pc(dbg, offset_pc);
  auto file_path = const_cast <char *>(line_entry->file->path.c_str());

  auto file_name = extract_file_name_from_path(file_path);

  return (Source_Location){file_path, file_name, line_entry->line};
}

Source_Context get_source_context(Debugger *dbg, u32 line_count) {
  auto current_pc = get_pc(dbg);
  auto offset_pc = offset_load_address(dbg, current_pc);
  auto line_entry = get_line_entry_from_pc(dbg, offset_pc);

  auto line = line_entry->line;

  auto context_start_line = line <= line_count ? 1 : line - line_count;
  auto context_end_line   = line + line_count + (line < line_count ? line - line_count : 0) + 1;

  auto file_path = const_cast <char *>(line_entry->file->path.c_str());

  auto file_name = extract_file_name_from_path(file_path);

  return (Source_Context){file_path, file_name, context_start_line, line, context_end_line};
}

void print_source_location(Source_Location * location) {
  auto fp = fopen(location->file_path, "r");
  defer { fclose(fp); };

  char c = 0;
  u32 current_line = 1;
  while (current_line != location->line && (c = fgetc(fp)) != EOF) {
    if (c == '\n')  current_line++;
  }

  printf("%s:%ld ", location->file_name, location->line);

  while ((c = fgetc(fp)) != EOF) {
    printf("%c", c);
    if (c == '\n')  break;
  }

  printf("\n");
}

void print_source_context(Source_Context * context) {
  auto fp = fopen(context->file_path, "r");
  defer { fclose(fp); };

  char c = 0;
  u32 current_line = 1;
  while (current_line != context->start_line && (c = fgetc(fp)) != EOF) {
    if (c == '\n')  current_line++;
  }

  printf("In file %s:\n", context->file_path);
  if (current_line == context->current_line)  printf(">%d ", current_line);
  else  printf(" %d ", current_line);

  while (current_line <= context->end_line && (c = fgetc(fp)) != EOF) {
    printf("%c", c);
    if (c == '\n') {
      current_line++;
      if (current_line == context->current_line)  printf(">%d ", current_line);
      else  printf(" %d ", current_line);
    }
  }

  printf("\n");
}

void print_current_source_location(Debugger * dbg) {
  auto location = get_source_location(dbg);
  print_source_location(&location);
}

void print_current_source_context(Debugger * dbg, u32 line_count) {
  auto context = get_source_context(dbg, line_count);
  print_source_context(&context);
}

void step_single_instruction(Debugger *dbg) {
  ptrace(PTRACE_SINGLESTEP, dbg->debugee_pid, nullptr, nullptr);
  wait_for_signal(dbg);
}

void step_over_breakpoint(Debugger *dbg) {
  auto res = dbg->breakpoint_map[get_pc(dbg)];
  if (!res)  return;
  auto bp = *res;

  if (bp && bp->enabled) {
    disable_breakpoint(dbg, bp);
    step_single_instruction(dbg);
    enable_breakpoint(dbg, bp);
  }
}

void single_instruction_step_with_breakpoint_check(Debugger *dbg) {
  bool on_breakpoint = dbg->breakpoint_map.exists(get_pc(dbg));

  if (on_breakpoint) {
    step_over_breakpoint(dbg);
  } else {
    step_single_instruction(dbg);
  }
}

void continue_execution(Debugger *dbg) {
  step_over_breakpoint(dbg);
  ptrace(PTRACE_CONT, dbg->debugee_pid, nullptr, nullptr);
  wait_for_signal(dbg);
}

void step_in(Debugger * dbg) {
  auto line = get_line_entry_from_pc(dbg, get_offset_pc(dbg))->line;

  while (get_line_entry_from_pc(dbg, get_offset_pc(dbg))->line == line) {
    single_instruction_step_with_breakpoint_check(dbg);
  }
}

void step_out(Debugger * dbg) {
  auto base_pointer = read_register(dbg, Register::rbp);
  auto return_address = read_memory(dbg, base_pointer + 8);

  bool return_breakpoint_exists = dbg->breakpoint_map.exists(return_address);

  bool should_remove_breakpoint = false;
  Breakpoint *return_breakpoint = nullptr;
  if (!return_breakpoint_exists) {
    return_breakpoint = set_breakpoint(dbg, return_address);
    should_remove_breakpoint = true;
  }

  continue_execution(dbg);

  if (should_remove_breakpoint) {
    remove_breakpoint(dbg, return_breakpoint);
  }
}

void step_over(Debugger * dbg) {
  auto func = get_function_from_pc(dbg, get_offset_pc(dbg));
  auto func_entry = at_low_pc(func);
  auto func_end = at_high_pc(func);

  auto line = get_line_entry_from_pc(dbg, func_entry);
  auto current_line = get_line_entry_from_pc(dbg, get_offset_pc(dbg));

  Array<Breakpoint *> to_delete;
  defer {
    For (to_delete) {
      remove_breakpoint(dbg, it);
    }
    to_delete.deinit();
  };

  while (line->address < func_end) {
    auto load_address = offset_dwarf_address(dbg, line->address);
    if (line->address != current_line->address) {
      bool exists_breakpoint_with_load_address = dbg->breakpoint_map.exists(load_address);

      if (!exists_breakpoint_with_load_address) {
        auto breakpoint = set_breakpoint(dbg, load_address);
        to_delete.add(breakpoint);
      }
    }

    ++line;
  }

  auto base_pointer = read_register(dbg, Register::rbp);
  auto return_address = read_memory(dbg, base_pointer + 8);

  bool return_breakpoint_exists = dbg->breakpoint_map.exists(return_address);

  if (!return_breakpoint_exists) {
    auto return_breakpoint = set_breakpoint(dbg, return_address);
    to_delete.add(return_breakpoint);
  }

  continue_execution(dbg);
}


class ptrace_expr_context : public dwarf::expr_context {
public:
  ptrace_expr_context(pid_t pid, u64 load_address) : pid(pid), load_address(load_address) {}

  dwarf::taddr reg(u32 regnum) override {
    return read_dwarf_register(pid, regnum);
  }
  dwarf::taddr pc() override {
    struct user_regs_struct regs;
    ptrace(PTRACE_GETREGS, pid, nullptr, &regs);
    return regs.rip - load_address;
  }
  dwarf::taddr deref_size(dwarf::taddr address, u32 size) override {
    return ptrace(PTRACE_PEEKDATA, pid, address + load_address, nullptr);
  }

private:
  pid_t pid;
  u64 load_address;
};

Array<Variable> get_variables(Debugger *dbg) {
  auto func = get_function_from_pc(dbg, offset_load_address(dbg, get_pc(dbg)));

  Array<Variable> variables;

  for (const auto &die : func) {
    if (die.tag == dwarf::DW_TAG::variable) {
      auto loc_val = die[dwarf::DW_AT::location];
      if (loc_val.get_type() == dwarf::value::type::exprloc) {
        ptrace_expr_context context(dbg->debugee_pid, dbg->load_address);
        auto result = loc_val.as_exprloc().evaluate(&context);

        switch (result.location_type) {
        case dwarf::expr_result::type::address: {
          auto value = read_memory(dbg, result.value);
          auto name = const_cast <char *>(die[dwarf::DW_AT::name].as_cstr(nullptr));
          auto &address = result.value;
          variables.add((Variable){name, value, Variable_Location::MEMORY, address});
          break;
        }

        case dwarf::expr_result::type::reg: {
          auto value = read_dwarf_register(dbg, result.value);
          auto name = const_cast <char *>(die[dwarf::DW_AT::name].as_cstr(nullptr));
          auto &reg = result.value;
          variables.add((Variable){name, value, Variable_Location::REGISTER, reg});
          break;
        }

        default:
          fprintf(stderr, "ERROR: Unhandled variable location\n");
          break;
        }
      }
    }
  }

  return variables;
}

void deinit(Array<Variable> variables) {
  variables.deinit();
}

void print_variables(Array<Variable> variables) {
  For (variables) {
    switch (it.location) {
    case Variable_Location::MEMORY:
      printf("%s\t(addr 0x%lx)\t= 0x%lx\n", it.name, it.address, it.value);
      break;
    case Variable_Location::REGISTER: {
      auto register_name = global_register_descriptors[(u32)it.reg].name;
      printf("%s\t(reg %s)\t= 0x%lx\n", it.name, register_name, it.value);
      break;
    }
    default:
      assert(false && "Unknownw variable location");
      break;
    }
  }
}

inline char *get_function_name(dwarf::die function_die) {
  if (function_die.has(dwarf::DW_AT::name))  return const_cast <char *>(function_die[dwarf::DW_AT::name].as_cstr(nullptr));
  else if (function_die.has(dwarf::DW_AT::specification)) {
    auto member_function_die = function_die[dwarf::DW_AT::specification].as_reference();
    return const_cast <char *>(member_function_die[dwarf::DW_AT::name].as_cstr(nullptr));
  }
  assert(false);
}

inline void add_function(Debugger *dbg, Array<Frame> *frames, dwarf::die function_die) {
  auto function_name = get_function_name(function_die);
  auto file_coordinates = function_die[dwarf::DW_AT::decl_file].as_uconstant();

  // Finding filename from declaration coordinates
  char *file_path = nullptr;
  for (auto &cu : dbg->dwarf.compilation_units()) {
    try {
      auto &lt = cu.get_line_table();
      auto file = lt.get_file(file_coordinates);
      file_path = const_cast <char *>(file->path.c_str());
      break;
    } catch (std::out_of_range e) { }
  }

  if (!file_path)  file_path = (char *)"??";

  auto address = dwarf::at_low_pc(function_die);
  auto line = function_die[dwarf::DW_AT::decl_line].as_uconstant();

  auto file_name = extract_file_name_from_path(file_path);

  auto function_location = (Source_Location){file_path, file_name, line};

  frames->add((Frame){function_name, function_location, address});
}

Array<Frame> get_stack_trace(Debugger * dbg) {
  Array<Frame> frames;

  auto func = get_function_from_pc(dbg, offset_load_address(dbg, get_pc(dbg)));
  add_function(dbg, &frames, func);

  auto frame_pointer = read_register(dbg, Register::rbp);
  auto return_address = read_memory(dbg, frame_pointer + 8);

  auto iter = func;
  char *iter_function_name = get_function_name(iter);
  while (strncmp(iter_function_name, "main", 5) != 0) {
    iter = get_function_from_pc(dbg, offset_load_address(dbg, return_address));
    add_function(dbg, &frames, iter);

    frame_pointer = read_memory(dbg, frame_pointer);
    return_address = read_memory(dbg, frame_pointer + 8);
    iter_function_name = get_function_name(iter);
  }

  return frames;
}

void deinit(Array<Frame> stack_trace) {
  stack_trace.deinit();
}

void print_stack_trace(Array<Frame> stack_trace) {
  u32 frame_number = 0;
  For (stack_trace) {
    printf("frame #%d: 0x%lx %s at %s:%ld\n", frame_number++, it.address, it.function_name, it.location.file_name, it.location.line);
  }
}
                             
///////////////////////////////
//
//  Symbol table
//

void deinit(Array<Symbol> symbols) {
  For (symbols) {
    if (it.name)  free(it.name);
  }
  symbols.deinit();
}

char *to_string(Symbol_Type type) {
  switch (type) {
  case Symbol_Type::NO_TYPE:     return (char *)"no type";
  case Symbol_Type::OBJECT:      return (char *)"object";
  case Symbol_Type::FUNCTION:    return (char *)"function";
  case Symbol_Type::SECTION:     return (char *)"section";
  case Symbol_Type::SOURCE_FILE: return (char *)"source file";
  }
  assert(false);
}

Symbol_Type to_symbol_type(elf::stt symbol) {
  switch (symbol) {
  case elf::stt::notype:   return Symbol_Type::NO_TYPE;
  case elf::stt::object:   return Symbol_Type::OBJECT;
  case elf::stt::func:     return Symbol_Type::FUNCTION;
  case elf::stt::section:  return Symbol_Type::SECTION;
  case elf::stt::file:     return Symbol_Type::SOURCE_FILE;
  default:                 return Symbol_Type::NO_TYPE;
  }
}

Array<Symbol> lookup_symbol(Debugger *dbg, const char *c_name) {
  Array<Symbol> syms;

  auto name = const_cast <char *>(c_name);

  auto name_len = strlen(name);
  for (auto &sec : dbg->elf.sections()) {
    if (sec.get_hdr().type != elf::sht::symtab && sec.get_hdr().type != elf::sht::dynsym)  continue;

    for (auto sym : sec.as_symtab()) {
      size_t symtab_name_len = -1;
      auto symtab_name = sym.get_name(&symtab_name_len);

      if (symtab_name_len > 0) {
        if (symtab_name_len >= 2 && symtab_name[0] == '_' && symtab_name[1] == 'Z') {
          s32 status = -1;

          char *demangled_name = abi::__cxa_demangle(symtab_name, NULL, NULL, &status);

          if (status == 0 && strstr(demangled_name, name)) {
            auto &d = sym.get_data();
            syms.add((Symbol){to_symbol_type(d.type()), demangled_name, d.value});
          } else {
            free(demangled_name); // Free, if not adding to syms array
          }
        } else {
          if (strstr(symtab_name, name)) {
            auto &d = sym.get_data();

            auto symbol_name = static_cast <char *>(malloc(symtab_name_len + 1));
            strncpy(symbol_name, symtab_name, symtab_name_len);
            symbol_name[symtab_name_len] = '\0';

            syms.add((Symbol){to_symbol_type(d.type()), symbol_name, d.value});
          }
        }
      }
    }
  }

  return syms;
}

DBG_NAMESPACE_END
