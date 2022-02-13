#include <unistd.h>

#include "defer.h"
#include "common.h"

#include "debugger.h"
#include "debugger.cpp"

s32 main(s32 argc, char *argv[]) {
  dbg::Debugger debugger;
  dbg::Debugger *d = &debugger;

  dbg::init(d);
  defer { dbg::deinit(d); };

  dbg::debug(d, "debugee", nullptr);

  // if (argc < 2)  return 1;
  // attach(d, atoi(argv[1]));

  dbg::set_breakpoint(d, 0x5555555553b0);
  dbg::remove_breakpoint(d, 0x5555555553b0);

  dbg::set_breakpoint(d, "debugee.cpp", 62);
  dbg::set_breakpoint(d, "debugee2_call()");
  dbg::set_breakpoint(d, "a()");

  dbg::set_breakpoint(d, "overloaded_func(long unsigned int, char *)");

  dbg::print_breakpoints(d);

  dbg::continue_execution(d);

  printf("START OF CONTEXT PRINT\n");
  dbg::print_current_source_context(d, 4);
  printf("END OF CONTEXT PRINT\n");

  dbg::continue_execution(d);
  auto stack_trace1 = dbg::get_stack_trace(d);
  defer { deinit(stack_trace1); };

  printf("Stack trace at ");
  dbg::print_current_source_location(d);
  dbg::print_stack_trace(stack_trace1);
  printf("\n");

  For_Count (6, i) {
    dbg::step_out(d);
  }

  dbg::step_over(d);
  dbg::print_current_source_location(d);
  dbg::step_over(d);
  dbg::print_current_source_location(d);

  dbg::step_single_instruction(d);

  auto rsp = dbg::read_register(d, dbg::Register::rsp);
  printf("rsp = %lx\n", rsp);

  auto mem = dbg::read_memory(d, 0x5555555553b0);
  printf("mem = %lx\n", mem);

  dbg::step_in(d);
  dbg::step_over(d);
  dbg::print_current_source_location(d);
  dbg::step_out(d);

  dbg::continue_execution(d);
  dbg::print_current_source_context(d, 1);

  dbg::print_current_source_location(d);
  auto locals1 = dbg::get_variables(d);
  defer { dbg::deinit(locals1); };

  print_variables(locals1);

  dbg::step_in(d);
  dbg::print_current_source_location(d);

  auto stack_trace2 = dbg::get_stack_trace(d);
  defer { dbg::deinit(stack_trace2); };

  dbg::print_stack_trace(stack_trace2);

  dbg::step_out(d);
  dbg::print_current_source_location(d);

  auto b1 = dbg::set_breakpoint(d, "many_args(char *, int, float, int ******)");
  dbg::remove_breakpoint(d, b1);

  dbg::set_breakpoint(d, "f");
  dbg::step_out(d);
  dbg::continue_execution(d);
  dbg::print_current_source_location(d);

  auto matched_names = dbg::lookup_symbol(d, "main");
  defer { dbg::deinit(matched_names); };

  printf("Symbols matched to main:\n");
  For (matched_names) {
    printf("0x%lx (%s): %s\n", it.address, to_string(it.type), it.name);
  }
  printf("\n");

  printf("Debugging session finished!\n");

  return 0;
}
