#include <sys/ptrace.h>
#include <sys/wait.h>
#include <sys/personality.h>
#include <sys/user.h>
// #include <linux/types.h>
// #include <sys/siginfo.h>
#include <unistd.h>
#include <iostream>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <fcntl.h>

#include <cxxabi.h>

#include "include/linenoise.h"
#include "dwarf/dwarf++.hh"
#include "elf/elf++.hh"

#include "common.h"
#include "defer.h"
#include "Array.h"
#include "Hash_Table.h"

#include "declaration_parser.h"

// #pragma GCC diagnostic ignored "-Wwrite-strings"

// Plan: Write out debugging lib, which can be used in ImGui graphical program
// * Implement iteration for hash table
// * Lib API
// * Remove exception throws
// * Handle process termination
// * Attach to a process
// * Add conditional breakpoints
// * Figure out how to deal with multiple breakpoints on one line. Currnetly setting up new breakpoint leak an old one.
// * process_vm_readv and process_vm_writev
// * ImGUI

struct Argument_String {
  char *start;
  s32 length = -1;
};

Array<Argument_String> split(char * line) {
  char * line_ptr = line;
  
  // Clip preceeding spaces
  while (*line_ptr != '\0' && isspace(*line_ptr))  line_ptr++;

  Array<Argument_String> args; // TODO: Change to String_Builder maybe
  args.add((Argument_String){line_ptr, -1});
  while (*line_ptr != '\0') {
    auto &last_string = args[args.count - 1];

    if (last_string.length == -1 && isspace(*line_ptr)) {
      last_string.length = line_ptr - last_string.start;
    }

    if (*(line_ptr + 1) != '\0') {
      if (isspace(*line_ptr) && !isspace(*(line_ptr + 1))) {
        args.add((Argument_String){line_ptr + 1, -1});
      }
    }
    
    line_ptr++;
  }

  // Clip trailing spaces
  while (isspace(*(line_ptr - 1)))  line_ptr--;

  auto &last_string = args[args.count - 1];
  last_string.length = line_ptr - last_string.start;

  return args;
}

Array<Argument_String> split(char * line, char splitter) {
  char * line_ptr = line;

  Array<Argument_String> args; // TODO: Change to String_Builder maybe
  args.add((Argument_String){line_ptr, -1});
  while (*line_ptr != '\0') {
    auto &last_string = args[args.count - 1];

    if (last_string.length == -1 && *line_ptr == splitter) {
      last_string.length = line_ptr - last_string.start;

      if (*(line_ptr + 1) != '\0') {
        args.add((Argument_String){line_ptr + 1, -1});
      }
    }
    
    line_ptr++;
  }

  return args;
}

bool is_suffix(char *str, const char *suffix) {
  if (!str || !suffix)  return false;

  auto str_len = strlen(str);
  auto suffix_len = strlen(str);

  char *str_suffix = (str + str_len) - suffix_len;

  return strncmp(str_suffix, suffix, suffix_len) == 0;
}

struct Breakpoint;
enum Register : u8;

// TODO: When gets complicated rewrite with member functions and compare implementations
// TODO: Move to separate debugger.h file
struct Debug_Info {
  char * program_name;
  u32 pid;

  Hash_Table<u64, Breakpoint *> breakpoints;

  dwarf::dwarf dwarf;
  elf::elf elf;

  u64 load_address = 0;
};

struct Breakpoint {
  Debug_Info *dbg;
  u64 address;
  bool enabled;
  u8 saved_instruction;
};

void debugger_write_register(Debug_Info *dbg, Register r, u64 value);
u64 debugger_read_register(Debug_Info *dbg, Register r);

void enable_breakpoint(Breakpoint *breakpoint);
void disable_breakpoint(Breakpoint *breakpoint);

inline u64 debugger_get_pc(Debug_Info *dbg);
inline void debugger_set_pc(Debug_Info *dbg, u64 pc);

u64 debugger_read_memory(Debug_Info *dbg, u64 address);
void debugger_write_memory(Debug_Info *dbg, u64 address, u64 value);

void enable_breakpoint(Breakpoint *breakpoint);
void disable_breakpoint(Breakpoint *breakpoint);

u64 debugger_offset_load_address(Debug_Info *dbg, u64 addr);
u64 debugger_offset_dwarf_address(Debug_Info *dbg, u64 addr);

dwarf::line_table::iterator debugger_get_line_entry_from_pc(Debug_Info *dbg, u64 pc);
dwarf::die debugger_get_function_from_pc(Debug_Info *dbg, u64 pc);

Breakpoint *debugger_set_breakpoint(Debug_Info *dbg, u64 address) {
  Breakpoint *breakpoint = static_cast<Breakpoint *> (malloc(sizeof(Breakpoint))); 
  breakpoint->dbg = dbg;
  breakpoint->enabled = false;
  breakpoint->address = address;

  dbg->breakpoints.insert(address, breakpoint);

  enable_breakpoint(breakpoint);

  return breakpoint;
}

void debugger_remove_breakpoint(Debug_Info *dbg, Breakpoint *breakpoint) {
  if (!breakpoint)  return;

  if (breakpoint->enabled) {
    disable_breakpoint(breakpoint);
  }

  breakpoint->dbg->breakpoints.remove(breakpoint->address); // :NullPointerInBreakpoints

  free(breakpoint);
}

bool debugger_remove_breakpoint_at_address(Debug_Info *dbg, u64 address) {
  Breakpoint **res = dbg->breakpoints[address];
  if (res) {
    auto breakpoint = *res;
    if (breakpoint->enabled)  disable_breakpoint(breakpoint);
    dbg->breakpoints.remove(address);

    if (breakpoint)  free(breakpoint);
    return true;
  } else {
    return false;
  }
}

void enable_breakpoint(Breakpoint *breakpoint) {
  if (breakpoint->enabled)  return;

  auto instruction = debugger_read_memory(breakpoint->dbg, breakpoint->address);

  breakpoint->saved_instruction = static_cast<u8> (instruction & 0xff);

  u64 int3 = 0xcc;
  u64 injected_int3 = ((instruction & ~0xff) | int3);
  debugger_write_memory(breakpoint->dbg, breakpoint->address, injected_int3);

  breakpoint->enabled = true;
}

void disable_breakpoint(Breakpoint *breakpoint) {
  if (!breakpoint->enabled)  return;

  auto instruction = debugger_read_memory(breakpoint->dbg, breakpoint->address);

  auto restored_instruction = ((instruction & ~0xff) | breakpoint->saved_instruction);

  debugger_write_memory(breakpoint->dbg, breakpoint->address, restored_instruction);

  breakpoint->enabled = false;
}

bool match_parameter_type(dwarf::die type_die, Function_Argument *parameter, u32 pointer_level = 0, bool is_const = false) {
  switch (type_die.tag) {
  case dwarf::DW_TAG::base_type:
    if (type_die.has(dwarf::DW_AT::name)) {
      std::string type_name;
      type_die[dwarf::DW_AT::name].as_string(type_name);
      if (parameter->pointer_level == pointer_level && parameter->is_const == is_const) {
        if (!parameter->is_compound_type) {
          return strncmp(type_name.c_str(), parameter->type_name, parameter->type_name_length) == 0;
        } else {
          char * type_part_start_pointer = parameter->type_name;
          char * type_part_end_pointer = parameter->type_name;
          auto type_part_offset = 0;
          auto die_type_part_offset = 0;

          while (type_part_offset < parameter->type_name_length) {
            while (isalnum(*type_part_end_pointer) || *type_part_end_pointer == '_')  type_part_end_pointer++;

            type_part_offset = (type_part_start_pointer - parameter->type_name);
            auto type_part_length = (type_part_end_pointer - type_part_start_pointer);
            auto names_match = (strncmp(type_name.c_str() + die_type_part_offset, type_part_start_pointer, type_part_length) == 0);

            if (!names_match)  return false;

            while (isspace(*type_part_end_pointer))  type_part_end_pointer++;

            type_part_start_pointer = type_part_end_pointer;
            die_type_part_offset += type_part_length + 1; // +1 for space between keywords
          }

          // Check if all parts of compound type were present in parsed parameter type
          if (type_part_offset >= type_name.size())  return true;
          else  return false;
        }
      } else {
        return false;
      }
    }
    break;

  case dwarf::DW_TAG::const_type:
    return match_parameter_type(dwarf::at_type(type_die), parameter, pointer_level, true);

  case dwarf::DW_TAG::pointer_type:
    return match_parameter_type(dwarf::at_type(type_die), parameter, pointer_level + 1);
  }

  return false;
}

bool match_function_declaration(dwarf::die function_die, Function_Declaration *declaration) {
  if (function_die.tag != dwarf::DW_TAG::subprogram)  return false;

  std::string die_function_name;
  if (function_die.has(dwarf::DW_AT::name)) {
    function_die[dwarf::DW_AT::name].as_string(die_function_name);
  } else if (function_die.has(dwarf::DW_AT::specification)) {
    auto member_function_die = at_specification(function_die);
    if (member_function_die.has(dwarf::DW_AT::name)) {
      member_function_die[dwarf::DW_AT::name].as_string(die_function_name);
    } else {
      return false;
    }
  } else {
    return false;
  }

  if (strncmp(die_function_name.c_str(), declaration->function_name, declaration->name_length) == 0) {
    if (declaration->arguments.count <= 0) {
      u32 parameter_count = 0;
      for (const auto &child : function_die) {
        if (child.tag != dwarf::DW_TAG::formal_parameter)  continue;
        parameter_count++;
      }

      // Check if die of the function also have no formal parameters
      if (parameter_count == 0)  return true;
      else  return false;
    } else {
      u32 parameter_index = 0;
      for (const auto &child : function_die) {
        if (child.tag != dwarf::DW_TAG::formal_parameter)  continue;
        if (parameter_index >= declaration->arguments.count)  break;

        auto &formal_parameter = child;
        auto type = dwarf::at_type(formal_parameter);
        if (!match_parameter_type(type, &(declaration->arguments[parameter_index]))) {
          break;
        }

        parameter_index++;
      }

      if (parameter_index == declaration->arguments.count) {
        // If we got to this point, all argument types were positively matched
        return true;
      }
    }
  }

  return false;
}

Breakpoint *debugger_set_breakpoint_at_function(Debug_Info *dbg, char *function_declaration_string) {
  auto function_declaration_len = strlen(function_declaration_string);

  auto declaration = parse_function_declaration(function_declaration_string, function_declaration_len);
  defer { deinit(&declaration); };

  if (declaration.function_name == nullptr) {
    std::cerr << "ERROR: Cannot parse function declaration" << std::endl;
    return nullptr;
  }

  for (const auto &cu : dbg->dwarf.compilation_units()) {
    for (const auto &die : cu.root()) {
      if (die.tag == dwarf::DW_TAG::subprogram) {
        if (match_function_declaration(die, &declaration)) {
          if (die.has(dwarf::DW_AT::low_pc)) {
            auto low_pc = at_low_pc(die);
            auto entry = debugger_get_line_entry_from_pc(dbg, low_pc);
            ++entry; // skip function prologue
            return debugger_set_breakpoint(dbg, debugger_offset_dwarf_address(dbg, entry->address));
          }
        }
      }
    }
  }

  return nullptr;
}

Breakpoint *debugger_set_breakpoint_at_source_line(Debug_Info *dbg, char *filename, u32 line) {
  for (const auto &cu : dbg->dwarf.compilation_units()) {
    if (is_suffix(filename, at_name(cu.root()).c_str())) {
      const auto &lt = cu.get_line_table();

      for (const auto &entry : lt) {
        if (entry.is_stmt && entry.line == line) {
          return debugger_set_breakpoint(dbg, debugger_offset_dwarf_address(dbg, entry.address));
        }
      }
    }
  }

  return nullptr;
}

enum Symbol_Type : u8 {
  notype,
  object,
  func,
  section,
  file
};

char *to_string(Symbol_Type type) {
  switch (type) {
  case Symbol_Type::notype:   return (char *)"notype";
  case Symbol_Type::object:   return (char *)"object";
  case Symbol_Type::func:     return (char *)"func";
  case Symbol_Type::section:  return (char *)"section";
  case Symbol_Type::file:     return (char *)"file";
  }
  assert(false);
}

Symbol_Type to_symbol_type(elf::stt symbol) {
  switch (symbol) {
  case elf::stt::notype:   return Symbol_Type::notype;
  case elf::stt::object:   return Symbol_Type::object;
  case elf::stt::func:     return Symbol_Type::func;
  case elf::stt::section:  return Symbol_Type::section;
  case elf::stt::file:     return Symbol_Type::file;
  default:                 return Symbol_Type::notype;
  }
}

struct Symbol {
  Symbol_Type type;
  char *name;
  u64 address;
};

Array<Symbol> debugger_lookup_symbol(Debug_Info *dbg, char *name) {
  Array<Symbol> syms;

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

            auto symbol_name = static_cast <char *>(malloc(symtab_name_len));
            strncpy(symbol_name, symtab_name, symtab_name_len);

            syms.add((Symbol){to_symbol_type(d.type()), symbol_name, d.value});
          }
        }
      }
    }
  }

  return syms;
}

enum Register : u8 {
  r15, r14, r13, r12,
  rbp, rbx,
  r11, r10, r9,  r8,
  rax, rcx, rdx, rsi, rdi,
  orig_rax,
  rip, cs, eflags, rsp, ss,
  fs_base, gs_base,
  ds, es, fs, gs, UNKNOWN
};


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

u64 debugger_read_register(pid_t pid, Register r) {
  if (r == Register::UNKNOWN)  assert(false && "Attempt to read from UNKNOWN register");

  user_regs_struct regs;
  // @Hack: We're get to kernel reading each register. Maybe it needs some caching or
  //        an option to read batch of registers with one ptrace call
  ptrace(PTRACE_GETREGS, pid, nullptr, &regs);

  return *(reinterpret_cast<u64 *> (&regs) + r);
}

u64 debugger_read_register(Debug_Info *dbg, Register r) {
  return debugger_read_register(dbg->pid, r);
}

void debugger_write_register(pid_t pid, Register r, u64 value) {
  if (r == Register::UNKNOWN)  assert(false && "Attempt to write to UNKNOWN register");

  user_regs_struct regs;
  // @Hack: We're get to kernel reading each register. Maybe it needs some caching or
  //        an option to read batch of registers with one ptrace call
  ptrace(PTRACE_GETREGS, pid, nullptr, &regs);

  *(reinterpret_cast<u64 *> (&regs) + r) = value;

  ptrace(PTRACE_SETREGS, pid, nullptr, &regs);
}

void debugger_write_register(Debug_Info *dbg, Register r, u64 value) {
  debugger_write_register(dbg->pid, r, value);
}

inline u64 debugger_get_pc(Debug_Info *dbg) {
  return debugger_read_register(dbg, Register::rip);
}

inline void debugger_set_pc(Debug_Info *dbg, u64 pc) {
  debugger_write_register(dbg, Register::rip, pc);
}

u64 debugger_read_dwarf_register(pid_t pid, u32 register_number) {
  const Register_Descriptor *match = nullptr;
  For_Count (registers_count, i) {
    if (global_register_descriptors[i].dwarf_r == register_number)  match = &(global_register_descriptors[i]);
  }

  if (match == nullptr) {
    std::cerr << "ERROR: Unknown dwarf register" << std::endl;
  }

  return debugger_read_register(pid, match->r);
}

u64 debugger_read_dwarf_register(Debug_Info *dbg, u32 register_number) {
  return debugger_read_dwarf_register(dbg->pid, register_number);
}

void debugger_write_dwarf_register(pid_t pid, u32 register_number, u64 value) {
  const Register_Descriptor *match = nullptr;
  For_Count (registers_count, i) {
    if (global_register_descriptors[i].dwarf_r == register_number)  match = &(global_register_descriptors[i]);
  }

  if (match == nullptr) {
    std::cerr << "ERROR: Unknown dwarf register" << std::endl;
    return;
  }

  debugger_write_register(pid, match->r, value);
}

void debugger_write_dwarf_register(Debug_Info *dbg, u32 register_number, u64 value) {
  debugger_write_dwarf_register(dbg->pid, register_number, value);
}

char *get_register_name(Register r) {
  return global_register_descriptors[r].name;
}

Register get_register_from_name(char *name) {
  const u32 name_length = strlen(name);

  const Register_Descriptor *match = nullptr;
  For_Count (registers_count, i) {
    if (strncmp(global_register_descriptors[i].name, name, name_length) == 0) {
      match = &(global_register_descriptors[i]);
    }
  }

  if (match == nullptr) {
    std::cerr << "ERROR: Unknown register name" << std::endl;
    return Register::UNKNOWN;
  }

  return match->r;
}

void debugger_dump_registers(Debug_Info *dbg) {
  printf("Name\t\tValue\n");
  For_Count (registers_count, i) {
    printf("%s\t\t0x%lx\n", global_register_descriptors[i].name, debugger_read_register(dbg, global_register_descriptors[i].r));
  }
}

void debugger_load_debug_info(Debug_Info *dbg) {
  auto fd = open(dbg->program_name, O_RDONLY);

  dbg->elf   = elf::elf(elf::create_mmap_loader(fd));
  dbg->dwarf = dwarf::dwarf(dwarf::elf::create_loader(dbg->elf));
}

dwarf::die debugger_get_function_from_pc(Debug_Info *dbg, u64 pc) {
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

dwarf::line_table::iterator debugger_get_line_entry_from_pc(Debug_Info *dbg, u64 pc) {
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

void debugger_initialize_load_address(Debug_Info *dbg) {
  // If dynamic library was loaded
  if (dbg->elf.get_hdr().type == elf::et::dyn) {
    char proc_map_path[64];
    sprintf(proc_map_path, "/proc/%d/maps", dbg->pid);

    auto fp = fopen(proc_map_path, "r");

    // TODO: When adding support for address randomization scan file more thoroughly
    u64 start_addr;
    u64 end_addr;
    fscanf(fp, "%lx-%lx", &start_addr, &end_addr);

    dbg->load_address = start_addr;
  }
}

u64 debugger_offset_load_address(Debug_Info *dbg, u64 addr) {
  return addr - dbg->load_address;
}

u64 debugger_offset_dwarf_address(Debug_Info *dbg, u64 addr) {
  return addr + dbg->load_address;
}

void debugger_print_source(Debug_Info *dbg, const char *filename, u32 line, u32 context_lines_count = 3) {
  auto fp = fopen(filename, "r");

  auto context_start_line = line <= context_lines_count ? 1 : line - context_lines_count;
  auto context_end_line   = line + context_lines_count + (line < context_lines_count ? line - context_lines_count : 0) + 1;

  char c = 0;
  u32 current_line = 1;
  while (current_line != context_start_line && (c = fgetc(fp)) != EOF) {
    if (c == '\n') {
      current_line++;
    }
  }

  if (current_line == line)  std::cout << "> ";
  else  std::cout << "  ";

  while (current_line <= context_end_line && (c = fgetc(fp)) != EOF) {
    std::cout << c;
    if (c == '\n') {
      current_line++;
      if (current_line == line)  std::cout << "> ";
      else  std::cout << "  ";
    }
  }

  std::cout << std::endl;
}

// TODO: Currently moved data goes through kernel space.
//       Task: rewrite this to process_vm_readv and process_vm_writev (https://man7.org/linux/man-pages/man2/process_vm_readv.2.html)
//          OR use /proc/<pid>/mem instead ptrace (compare options' performance and portability to other systems)
u64 debugger_read_memory(Debug_Info *dbg, u64 address) {
  return ptrace(PTRACE_PEEKDATA, dbg->pid, address, nullptr);
}

void debugger_write_memory(Debug_Info *dbg, u64 address, u64 value) {
  ptrace(PTRACE_POKEDATA, dbg->pid, address, value);
}

siginfo_t debugger_get_signal_info(Debug_Info *dbg) {
  siginfo_t info;
  ptrace(PTRACE_GETSIGINFO, dbg->pid, nullptr, &info);
  return info;
}

void debugger_handle_sigtrap(Debug_Info *dbg, siginfo_t info) {
  switch (info.si_code) {
  case SI_KERNEL:
  case TRAP_BRKPT: {
    debugger_set_pc(dbg, debugger_get_pc(dbg) - 1);
    auto current_pc = debugger_get_pc(dbg);
    printf("Hit breakpoint at adress 0x%lx\n", current_pc);
    auto offset_pc = debugger_offset_load_address(dbg, current_pc);
    auto line_entry = debugger_get_line_entry_from_pc(dbg, offset_pc);
    debugger_print_source(dbg, line_entry->file->path.c_str(), line_entry->line);
    return;
  }
  case TRAP_TRACE: // single stepping trap
    return;
  case SI_USER:
    printf("Got SI_USER!!!!\n");
    break;
  default:
    std::cout << "Unknown SIGTRAP code: " << info.si_code << std::endl;
    return;
  }
}

void debugger_wait_for_signal(Debug_Info *dbg) {
  s32 wait_status;
  s32 options = 0;
  waitpid(dbg->pid, &wait_status, options);

  auto siginfo = debugger_get_signal_info(dbg);

  switch (siginfo.si_signo) {
  case SIGTRAP:
    debugger_handle_sigtrap(dbg, siginfo);
    break;
  case SIGSEGV:
    std::cout << "SEGFAULT! Reason: " << siginfo.si_code << std::endl;
    break;
  default:
    std::cout << "Got signal " << strsignal(siginfo.si_signo) << std::endl;
    break;
  }
}

void debugger_single_instruction_step(Debug_Info *dbg) {
  ptrace(PTRACE_SINGLESTEP, dbg->pid, nullptr, nullptr);
  debugger_wait_for_signal(dbg);
}

void debugger_step_over_breakpoint(Debug_Info *dbg) {
  auto res = dbg->breakpoints[debugger_get_pc(dbg)];
  if (!res)  return;
  auto bp = *res;

  if (bp && bp->enabled) {
    disable_breakpoint(bp);
    debugger_single_instruction_step(dbg);
    enable_breakpoint(bp);
  }
}

void debugger_single_instruction_step_with_breakpoint_check(Debug_Info *dbg) {
  bool on_breakpoint = dbg->breakpoints.exists(debugger_get_pc(dbg));

  if (on_breakpoint) {
    debugger_step_over_breakpoint(dbg);
  } else {
    debugger_single_instruction_step(dbg);
  }
}

u64 debugger_get_offset_pc(Debug_Info *dbg) {
  return debugger_offset_load_address(dbg, debugger_get_pc(dbg));
}

void debugger_step_in(Debug_Info *dbg) {
  auto line = debugger_get_line_entry_from_pc(dbg, debugger_get_offset_pc(dbg))->line;

  while (debugger_get_line_entry_from_pc(dbg, debugger_get_offset_pc(dbg))->line == line) {
    debugger_single_instruction_step_with_breakpoint_check(dbg);
  }

  // :NotLib
  // @Note: Not lib-feature at all
  auto line_entry = debugger_get_line_entry_from_pc(dbg, debugger_get_offset_pc(dbg));
  debugger_print_source(dbg, line_entry->file->path.c_str(), line_entry->line); 
}

void debugger_continue_execution(Debug_Info *dbg) {
  debugger_step_over_breakpoint(dbg);
  ptrace(PTRACE_CONT, dbg->pid, nullptr, nullptr);
  debugger_wait_for_signal(dbg);
}

void debugger_step_out(Debug_Info *dbg) {
  auto base_pointer = debugger_read_register(dbg, Register::rbp);
  auto return_address = debugger_read_memory(dbg, base_pointer + 8);

  bool return_breakpoint_exists = dbg->breakpoints.exists(return_address);

  bool should_remove_breakpoint = false;
  Breakpoint *return_breakpoint = nullptr;
  if (!return_breakpoint_exists) {
    return_breakpoint = debugger_set_breakpoint(dbg, return_address);
    should_remove_breakpoint = true;
  }

  debugger_continue_execution(dbg);

  if (should_remove_breakpoint) {
    debugger_remove_breakpoint(dbg, return_breakpoint);
  }
}

void debugger_step_over(Debug_Info *dbg) {
  auto func = debugger_get_function_from_pc(dbg, debugger_get_offset_pc(dbg));
  auto func_entry = at_low_pc(func);
  auto func_end = at_high_pc(func);

  auto line = debugger_get_line_entry_from_pc(dbg, func_entry);
  auto current_line = debugger_get_line_entry_from_pc(dbg, debugger_get_offset_pc(dbg));

  Array<Breakpoint *> to_delete;
  defer {
    For (to_delete) {
      debugger_remove_breakpoint(dbg, it);
    }
    to_delete.deinit();
  };

  while (line->address < func_end) {
    auto load_address = debugger_offset_dwarf_address(dbg, line->address);
    if (line->address != current_line->address) {
      bool exists_breakpoint_with_load_address = dbg->breakpoints.exists(load_address);

      if (!exists_breakpoint_with_load_address) {
        auto breakpoint = debugger_set_breakpoint(dbg, load_address);
        to_delete.add(breakpoint);
      }
    }

    ++line;
  }

  auto base_pointer = debugger_read_register(dbg, Register::rbp);
  auto return_address = debugger_read_memory(dbg, base_pointer + 8);

  bool return_breakpoint_exists = dbg->breakpoints.exists(return_address);

  if (!return_breakpoint_exists) {
    auto return_breakpoint = debugger_set_breakpoint(dbg, return_address);
    to_delete.add(return_breakpoint);
  }

  debugger_continue_execution(dbg);
}

inline void get_function_name(dwarf::die function_die, std::string &function_name) {
  if (function_die.has(dwarf::DW_AT::name))  function_die[dwarf::DW_AT::name].as_string(function_name);
  else if (function_die.has(dwarf::DW_AT::specification)) {
    auto member_function_die = function_die[dwarf::DW_AT::specification].as_reference();
    member_function_die[dwarf::DW_AT::name].as_string(function_name);
  }
}

inline void output_frame(dwarf::die function, bool new_backtrace = false) {
  static u32 frame_number = 0;
  if (new_backtrace)  frame_number = 0;

  std::string function_name;
  get_function_name(function, function_name);

  printf("frame #%d: 0x%lx %s\n", frame_number++, dwarf::at_low_pc(function), function_name.c_str());
}

// :NotLib
void debugger_print_backtrace(Debug_Info *dbg) {
  auto func = debugger_get_function_from_pc(dbg, debugger_offset_load_address(dbg, debugger_get_pc(dbg)));
  output_frame(func, true);

  auto frame_pointer = debugger_read_register(dbg, Register::rbp);
  auto return_address = debugger_read_memory(dbg, frame_pointer + 8);

  auto iter = func;
  std::string iter_function_name;
  get_function_name(iter, iter_function_name);
  while (strncmp(iter_function_name.c_str(), "main", 5) != 0) {
    iter = debugger_get_function_from_pc(dbg, debugger_offset_load_address(dbg, return_address));
    output_frame(iter);
    frame_pointer = debugger_read_memory(dbg, frame_pointer);
    return_address = debugger_read_memory(dbg, frame_pointer + 8);

    get_function_name(iter, iter_function_name);
  }
}

class ptrace_expr_context : public dwarf::expr_context {
public:
  ptrace_expr_context(pid_t pid, u64 load_address) : pid(pid), load_address(load_address) {}

  dwarf::taddr reg(u32 regnum) override {
    return debugger_read_dwarf_register(pid, regnum);
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

// :NotLib
void debugger_read_variables(Debug_Info *dbg) {
  auto func = debugger_get_function_from_pc(dbg, debugger_offset_load_address(dbg, debugger_get_pc(dbg)));

  for (const auto &die : func) {
    if (die.tag == dwarf::DW_TAG::variable) {
      auto loc_val = die[dwarf::DW_AT::location];
      if (loc_val.get_type() == dwarf::value::type::exprloc) {
        ptrace_expr_context context(dbg->pid, dbg->load_address);
        auto result = loc_val.as_exprloc().evaluate(&context);

        switch (result.location_type) {
        case dwarf::expr_result::type::address: {
          auto value = debugger_read_memory(dbg, result.value);
          printf("%s (0x%lx) = 0x%lx\n", dwarf::at_name(die).c_str(), result.value, value);
          break;
        }
        case dwarf::expr_result::type::reg: {
          auto value = debugger_read_dwarf_register(dbg, result.value);
          printf("%s (reg %s) = 0x%lx\n", dwarf::at_name(die).c_str(), global_register_descriptors[result.value].name, value);
          break;
        }
        default:
          std::cerr << "ERROR: Unhandled variable location" << std::endl;
        }
      }
    }
  }
}

// TODO: Implement attach to a processes
// TODO: Add debugger termination handling and kill tracee along with it
bool handle_command(Debug_Info *dbg, char * line) {
  auto args = split(line);
  defer { args.deinit(); };

  auto &command = args[0];

  // TODO: Repeating last commmand on empty input
  if (command.length == 0)  return true;

  // TODO: Write parse_command()?
  // switch (parse_command(command)) {
  // case DBG_CONTINUE:
  //   break;
  // case DBG_BREAK:
  //   break;
  // }
  switch (command.start[0]) {
  case 'q': // QUIT
    return false;
    break;

  case 'c': // CONTINUE
    debugger_continue_execution(dbg);
    break;

  case 'b': { // BREAK
    if (args.count < 2) {
      switch (command.start[1]) {
      case 't':
        debugger_print_backtrace(dbg);
        break;
        
      default: {
        std::cout << "Currently set breakpoints: " << std::endl;
        std::cout << "Not impemented for hash table yet! " << std::endl;

        // TODO: Implement iteration for hash table
        // u32 count = 0;
        // For (dbg->breakpoints) {
        //   printf("Breakpoint #%d at address 0x%lx; enabled status: %d\n", count++, it->address, it->enabled);
        // }
        break;
      }
      }
    } else {
      if (args[1].start[0] == '0' && args[1].start[1] == 'x' && args[1].length > 2) {
        char *str_hex = args[1].start + 2;
        auto breakpoint = debugger_set_breakpoint(dbg, std::stol(str_hex, 0, 16));

        std::cout << "Breakpoint's been set on " << args[1].start << std::endl;
      } else if (strstr(args[1].start, ":")) {
        auto file_and_line = split(args[1].start, ':');
        defer { file_and_line.deinit(); };

        char filename[256];
        strncpy(filename, file_and_line[0].start, file_and_line[0].length);
        auto line = std::stoi(file_and_line[1].start);

        auto breakpoint = debugger_set_breakpoint_at_source_line(dbg, filename, line);
        if (breakpoint) {
          std::cout << "Breakpoint's been set in file " << filename << " on line " << line  << std::dec << "; address 0x" << std::hex << breakpoint->address << std::endl;
        } else {
          std::cerr << "ERROR: Cannot set breakpoint" << std::endl;
        }
      } else {
        auto breakpoint = debugger_set_breakpoint_at_function(dbg, args[1].start);

        if (breakpoint) {
          std::cout << "Breakpoint's been set on function; address 0x" << std::hex << breakpoint->address << std::endl;
        } else {
          std::cerr << "ERROR: Cannot set breakpoint" << std::endl;
        }
      }
    }
    break;
  }

  case 'd':
    if (args.count > 1) {
      if (args[1].start[0] == '0' && args[1].start[1] == 'x' && args[1].length > 2) {
        char *str_hex = args[1].start + 2;
        auto success = debugger_remove_breakpoint_at_address(dbg, std::stol(str_hex, 0, 16));

        if (success) {
          std::cout << "Breakpoint's " << args[1].start << " been removed." << std::endl;
        } else {
          std::cerr << "ERROR: Cannot delete breakpoint" << std::endl;
        }
      }
    } else {
      std::cerr << "USAGE: d breakpoint_address" << std::endl;
    }
    break;

  case 's':
    if (command.length > 1) {
    switch (command.start[1]) {
    case 'i': // STEP SINGLE INSTRUCTION
      debugger_single_instruction_step_with_breakpoint_check(dbg);
      break;
    case 'y': // SYMBOL
      if (args.count < 2) {
        std::cerr << "ERROR: symbol command usage: symbol symbol_name" << std::endl;
        break;
      }

      auto syms = debugger_lookup_symbol(dbg, args[1].start);
      defer {
        For (syms) {
          if (it.name) {
            free(it.name);
          }
        }
        syms.deinit();
      };

      std::cout << "MATCHED SYMBOLS: " << std::endl;
      std::cout << "TYPE\tNAME\tADDRESS" << std::endl;
      For (syms) {
        printf("%s\t%s\t0x%lx\n", to_string(it.type), it.name, it.address);
      }

      break;
    }
    } else { // STEP SINGLE SOURCE LINE
      debugger_step_in(dbg);
    }
    break;

  case 'n': // NEXT
    debugger_step_over(dbg);
    break;

  case 'f': // FINISH
    debugger_step_out(dbg);
    break;

  case 'l': // LOCALS
    debugger_read_variables(dbg);
    break;

  case 'r': // REGISTER
    if (args.count < 2) {
      std::cerr << "ERROR: register command usage: r[egister] (dump|read|write) [argument]" << std::endl;
      break;
    }

    switch (args[1].start[0]) {
    case 'd':
      debugger_dump_registers(dbg);
      break;

    case 'r': {
      if (args.count < 3) {
        std::cerr << "ERROR: register read command usage: r[egister] r[ead] register_name" << std::endl;
        break;
      }

      auto reg = get_register_from_name(args[2].start);
      if (reg != Register::UNKNOWN) {
        printf("0x%lx\n", debugger_read_register(dbg, reg));
      }
      break;
    }

    case 'w': {
      if (args.count < 4) {
        std::cerr << "ERROR: register write command usage: r[egister] w[rite] register_name value" << std::endl;
        break;
     }

      char reg_name[9]; // @Hack: Refactor command parsing to really split entered string
      strncpy(reg_name, args[2].start, args[2].length);
      auto reg = get_register_from_name(reg_name);

      auto &value_arg = args[3];
      if (!(value_arg.start[0] == '0' && value_arg.start[1] == 'x' && value_arg.length > 2)) {
        std::cerr << "ERROR: Incorrect hex format in value argument" << std::endl;
        break;
      }

      char *val = args[3].start + 2;
      if (reg != Register::UNKNOWN) {
	debugger_write_register(dbg, reg, std::stol(val, 0, 16));
      }
      break;
    }
    }
    break;

  case 'm': // MEMORY
    if (args.count < 2) {
      std::cerr << "ERROR: memory command usage: m[emory] (read|write) [argument]" << std::endl;
      break;
    }

    switch (args[1].start[0]) {
    case 'r': {
      if (args.count < 3) {
        std::cerr << "ERROR: memory read command usage: m[emory] r[ead] address" << std::endl;
        break;
      }

      auto &address_arg = args[2];
      if (!(address_arg.start[0] == '0' && address_arg.start[1] == 'x' && address_arg.length > 2)) {
        std::cerr << "ERROR: Incorrect hex format in address argument" << std::endl;
        break;
      }

      char *address = args[2].start + 2;
      printf("0x%lx\n", debugger_read_memory(dbg, std::stol(address, 0, 16)));
      break;
    }

    case 'w': {
      if (args.count < 4) {
        std::cerr << "ERROR: memory write command usage: m[emory] w[rite] address value" << std::endl;
        break;
      }

      auto &address_arg = args[2];
      if (!(address_arg.start[0] == '0' && address_arg.start[1] == 'x' && address_arg.length > 2)) {
        std::cerr << "ERROR: Incorrect hex format in address argument" << std::endl;
        break;
      }

      auto &value_arg = args[3];
      if (!(value_arg.start[0] == '0' && value_arg.start[1] == 'x' && value_arg.length > 2)) {
        std::cerr << "ERROR: Incorrect hex format in value argument" << std::endl;
        break;
      }

      char *address = args[2].start + 2;
      char *val = args[3].start + 2;
      debugger_write_memory(dbg, std::stol(address, 0, 16), std::stol(val, 0, 16));
      break;
    }
    }
    break;

  default:
    std::cerr << "ERROR: Unknown command" << std::endl;
    break;
  }

  return true;
}

void debugger_loop(Debug_Info *dbg) {
  debugger_load_debug_info(dbg);
  debugger_wait_for_signal(dbg);
  debugger_initialize_load_address(dbg);

  // @Temporary: Move from CLI based debugger to ImGUI one
  char *line = nullptr;
  while ((line = linenoise("dbg> ")) != nullptr) {
    auto continue_run = handle_command(dbg, line);

    if (!continue_run)  break;

    linenoiseHistoryAdd(line);
    linenoiseFree(line);
  }
}

s32 main(s32 argc, char **argv) {
  if (argc < 2) {
    std::cerr << "ERROR: Program has not specified" << std::endl;
    return -1;
  }

  auto program_name = argv[1];
  auto pid = fork();
  if (pid == 0) {
    personality(ADDR_NO_RANDOMIZE);

    ptrace(PTRACE_TRACEME, 0, nullptr, nullptr);
    execl(program_name, program_name, nullptr);
  } else if (pid >= 1) {
    std::cout << "Started debugging process " << pid << std::endl;

    Debug_Info *info = static_cast<Debug_Info *> (malloc(sizeof(Debug_Info)));
    info->program_name = program_name;
    info->pid = pid;

    debugger_loop(info);

    // @Leak: Not freeing internal pointers, and don't care that much
    info->breakpoints.deinit();
    free(info);
  }
}
