#pragma once
#ifndef DEBUGGER_H
#define DEBUGGER_H

#define DBG_NAMESPACE_BEGIN namespace dbg {
#define DBG_NAMESPACE_END }

#include "dwarf/dwarf++.hh"
#include "elf/elf++.hh"

#include "common.h"
#include "Array.h"
#include "Hash_Table.h"

//
//  Debugger library interface
//

DBG_NAMESPACE_BEGIN

struct Debugger;
struct Breakpoint;
struct Break_Condition;
struct Source_Location;
struct Source_Context;
struct Symbol;
struct Frame;
struct Variable;
struct Source_File;

enum class Debugger_State : u8 {
  NOT_STARTED,
  ATTACHED,
  RUNNING,
  FINISHED // TODO: Handle finished state correctly
};

enum class Command_Status : u8 {
  NO_STATUS,
  SUCCESS,
  FAIL,
};

struct Source_File {
  char *file_path = nullptr;
  char *file_name = nullptr;

  u64 length;

  Array<char *> lines;

  char *content = nullptr;
};

struct Debugger {
  u32 debugee_pid = 0;
  char * executable_path = nullptr;
  char * argument_string = nullptr;

  Debugger_State state = Debugger_State::NOT_STARTED;
  Command_Status last_command_status = Command_Status::NO_STATUS;

  Array<Breakpoint *> breakpoints;
  Hash_Table<u64, Breakpoint *> breakpoint_map;

  dwarf::dwarf dwarf;
  elf::elf elf;

  Array<Source_File> source_files;

  u64 load_address = 0;
  bool verbose = false;
};

void init(Debugger * dbg);
void deinit(Debugger * dbg);

void debug(Debugger * dbg, const char * executable_path, const char * arguments);
void attach(Debugger * dbg, u32 pid);

void unload(Debugger * dbg);

//
// Reading and writing
//
enum class Register : u8 {
  r15, r14, r13, r12,
  rbp, rbx,
  r11, r10, r9,  r8,
  rax, rcx, rdx, rsi, rdi,
  orig_rax,
  rip, cs, eflags, rsp, ss,
  fs_base, gs_base,
  ds, es, fs, gs, UNKNOWN
};

u64 read_register(Debugger * dbg, Register reg);
void write_register(Debugger * dbg, Register reg, u64 value);

u64 read_memory(Debugger * dbg, u64 address);
void write_memory(Debugger * dbg, u64 address, u64 value);

//
// Location discovery
//

struct Source_Location {
  char *file_path = nullptr;
  char *file_name = nullptr;

  u64 line;
};

struct Source_Context {
  char *file_path = nullptr;
  char *file_name = nullptr;

  u64 start_line;
  u64 current_line;
  u64 end_line;
};

void print_sources(Array<Source_File> sources);

Source_Location get_source_location(Debugger * dbg);
Source_Context get_source_context(Debugger * dbg, u32 line_count = 3);
Array<Source_File> get_updated_sources(Debugger * dbg);

void print_source_location(Source_Location * location);
void print_source_context(Source_Context * context);

void print_current_source_location(Debugger * dbg);
void print_current_source_context(Debugger * dbg, u32 line_count = 3);

//
// Breakpoints
//
struct Breakpoint {
  Source_Location location;
  u64 address;

  bool enabled = false;

  Break_Condition * condition = nullptr;

  u8 saved_instruction;
};

enum class Break_Condition_Type : u8 {
  EQUALS      = 1,
  NOT_EQUALS  = 2,
  LESS        = 4,
  GREATER     = 8
};

enum class Break_Condition_Source_Type : u8 {
  MEMORY,
  REGISTER,
  LOCAL_VARIABLE
};

struct Break_Condition {
  Break_Condition_Type type;

  Break_Condition_Source_Type source_type;
  union {
    Register reg;
    u64 address;
    char *variable_name = nullptr;
  };

  u64 value;
};

Breakpoint *set_breakpoint(Debugger * dbg, u64 address);
Breakpoint *set_breakpoint(Debugger * dbg, const char * function_declaration);
Breakpoint *set_breakpoint(Debugger * dbg, const char * filename, u32 line);

void add_break_condition(Breakpoint * breakpoint, Register reg, u64 value, Break_Condition_Type condition_type);
void add_break_condition(Breakpoint * breakpoint, u64 address,  u64 value, Break_Condition_Type condition_type);
void add_break_condition(Breakpoint * breakpoint, const char * variable_name, u64 value, Break_Condition_Type condition_type);
void remove_break_condition(Breakpoint * breakpoint);

// TODO: Maybe add watchpoints

void enable_breakpoint(Debugger * dbg, Breakpoint * breakpoint);
void disable_breakpoint(Debugger * dbg, Breakpoint * breakpoint);

void remove_breakpoint(Debugger * dbg, Breakpoint * breakpoint);
void remove_breakpoint(Debugger * dbg, u64 address);

void print_breakpoints(Debugger * dbg);

//
// Symbols search
//
enum class Symbol_Type : u8 {
  NO_TYPE,
  OBJECT,
  FUNCTION,
  SECTION,
  SOURCE_FILE
};

char * to_string(Symbol_Type type);
Symbol_Type to_symbol_type(elf::stt symbol);

struct Symbol {
  Symbol_Type type;
  char *name = nullptr;
  u64 address;
};

Array<Symbol> lookup_symbol(Debugger * dbg, const char * name);
void deinit(Array<Symbol> symbol_table); // Symbols table should be freed after the use

//
// Stepping
//
void start(Debugger *dbg);
void stop(Debugger *dbg);

void continue_execution(Debugger * dbg);
void step_in(Debugger * dbg);
void step_out(Debugger * dbg);
void step_over(Debugger * dbg);
void step_single_instruction(Debugger * dbg);

//
// Stack trace
//
struct Frame {
  char *function_name = nullptr;
  Source_Location location;
  u64 address;
};

Array<Frame> get_stack_trace(Debugger * dbg);
void deinit(Array<Frame> stack_trace); // Stack trace should be freed after the use

void print_stack_trace(Array<Frame> stack_trace);

//
// Variables
//
enum class Variable_Location : u8 {
  MEMORY,
  REGISTER
};

// TODO: Maybe intern strings for variable and function names?
struct Variable {
  char *name = nullptr;
  u64 value;
  Variable_Location location;

  union {
    u64 address;
    Register reg;
  };
};

Array<Variable> get_variables(Debugger * dbg);
void deinit(Array<Variable> variables); // Varables should be freed after the use

void print_variables(Array<Variable> variables);

DBG_NAMESPACE_END

#endif
