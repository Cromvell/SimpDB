DBG_NAMESPACE_BEGIN

void enable_breakpoint(Debugger *dbg, Breakpoint *breakpoint) {
  if (dbg->state == Debugger_State::NOT_STARTED) {
    dbg_fail("debugged program isn't loaded");
    return;
  }

  if (breakpoint->enabled) {
    dbg_fail("breakpoint already enabled");
    return;
  }

  auto instruction = read_memory(dbg, breakpoint->address);

  breakpoint->saved_instruction = static_cast<u8> (instruction & 0xff);

  u64 int3 = 0xcc;
  u64 injected_int3 = ((instruction & ~0xff) | int3);
  write_memory(dbg, breakpoint->address, injected_int3);

  breakpoint->enabled = true;

  dbg_success();
}

void disable_breakpoint(Debugger *dbg, Breakpoint *breakpoint) {
  if (dbg->state == Debugger_State::NOT_STARTED) {
    dbg_fail("debugged program isn't loaded");
    return;
  }

  if (!breakpoint->enabled) {
    dbg_fail("breakpoint already disabled");
    return;
  }

  auto instruction = read_memory(dbg, breakpoint->address);

  auto restored_instruction = ((instruction & ~0xff) | breakpoint->saved_instruction);

  write_memory(dbg, breakpoint->address, restored_instruction);

  breakpoint->enabled = false;

  dbg_success();
}

/////////////////////////////////////
//
//  Breakpoint at address
//

Breakpoint *set_breakpoint(Debugger *dbg, u64 address) {
  if (dbg->state == Debugger_State::NOT_STARTED) {
    dbg_fail("debugged program isn't loaded");
    return nullptr;
  }

  Breakpoint *breakpoint = static_cast<Breakpoint *> (malloc(sizeof(Breakpoint))); 
  breakpoint->enabled = false;
  breakpoint->address = address;

  dbg->breakpoints.add(breakpoint);
  dbg->breakpoint_map.insert(address, breakpoint);

  enable_breakpoint(dbg, breakpoint);

  dbg_success();
  return breakpoint;
}

void remove_breakpoint(Debugger *dbg, Breakpoint *breakpoint) {
  if (dbg->state == Debugger_State::NOT_STARTED) {
    dbg_fail("debugged program isn't loaded");
    return;
  }

  if (!breakpoint) {
    dbg_fail("given breakpoint pointer was deinitialized and resetted to null");
    return;
  }

  if (breakpoint->enabled) {
    disable_breakpoint(dbg, breakpoint);
  }

  dbg->breakpoint_map.remove(breakpoint->address);

  auto breakpoint_index = dbg->breakpoints.find_index(breakpoint);
  if (breakpoint_index == -1) {
    dbg_fail("couldn't find breakpoint");
    return;
  }

  dbg->breakpoints.remove_unordered(breakpoint_index);

  free(breakpoint);
  dbg_success();
}


void remove_breakpoint(Debugger *dbg, u64 address) {
  if (dbg->state == Debugger_State::NOT_STARTED) {
    dbg_fail("debugged program isn't loaded");
    return;
  }

  Breakpoint **res = dbg->breakpoint_map[address];
  if (!res) {
    dbg_fail("breakpoint with given address wasn't found");
    return;
  }

  auto breakpoint = *res;
  if (!breakpoint)  {
    dbg_fail("found breakpoint pointer was deinitialized and resetted to null");
    return;
  }

  if (breakpoint->enabled)  disable_breakpoint(dbg, breakpoint);
  dbg->breakpoint_map.remove(address);

  auto breakpoint_index = dbg->breakpoints.find_index(breakpoint);
  assert(breakpoint_index != -1);
  dbg->breakpoints.remove_unordered(breakpoint_index);

  free(breakpoint);
  dbg_success();
}


/////////////////////////////////////
//
//  Breakpoint at function
//

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

Breakpoint *set_breakpoint(Debugger *dbg, const char *c_function_declaration_string) {
  if (dbg->state == Debugger_State::NOT_STARTED) {
    dbg_fail("debugged program isn't loaded");
    return nullptr;
  }

  auto function_declaration_string = const_cast <char *>(c_function_declaration_string);

  auto function_declaration_len = strlen(function_declaration_string);

  auto declaration = parse_function_declaration(function_declaration_string, function_declaration_len);
  defer { deinit(&declaration); };

  if (declaration.function_name == nullptr) {
    dbg_fail("cannot parse function declaration");
    return nullptr;
  }

  for (const auto &cu : dbg->dwarf.compilation_units()) {
    for (const auto &die : cu.root()) {
      if (die.tag == dwarf::DW_TAG::subprogram) {
        if (match_function_declaration(die, &declaration)) {
          if (die.has(dwarf::DW_AT::low_pc)) {
            auto low_pc = at_low_pc(die);
            auto line_entry = get_line_entry_from_pc(dbg, low_pc);
            ++line_entry; // skip function prologue
            auto bp = set_breakpoint(dbg, offset_dwarf_address(dbg, line_entry->address));

            const auto &lt = cu.get_line_table();
            auto file = lt.get_file(line_entry->file_index);
            bp->location.file_path = const_cast <char *>(file->path.c_str());
            bp->location.file_name = extract_file_name_from_path(bp->location.file_path);
            bp->location.line = line_entry->line;

            dbg_success();
            return bp;
          }
        }
      }
    }
  }

  dbg_fail("couldn't find specified function");
  return nullptr;
}


/////////////////////////////////////
//
//  Breakpoint at source line
//

bool is_suffix(char *str, const char *suffix) {
  if (!str || !suffix)  return false;

  auto str_len = strlen(str);
  auto suffix_len = strlen(str);

  char *str_suffix = (str + str_len) - suffix_len;

  return strncmp(str_suffix, suffix, suffix_len) == 0;
}

Breakpoint *set_breakpoint(Debugger *dbg, const char *c_file_name, u32 line) {
  if (dbg->state == Debugger_State::NOT_STARTED) {
    dbg_fail("debugged program isn't loaded");
    return nullptr;
  }

  auto file_name = const_cast <char *>(c_file_name);

  for (const auto &cu : dbg->dwarf.compilation_units()) {
    if (is_suffix(file_name, at_name(cu.root()).c_str())) {
      const auto &lt = cu.get_line_table();

      for (const auto &line_entry : lt) {
        if (line_entry.is_stmt && line_entry.line == line) {
          auto bp = set_breakpoint(dbg, offset_dwarf_address(dbg, line_entry.address));

          auto file = lt.get_file(line_entry.file_index);
          bp->location.file_path = const_cast <char *>(file->path.c_str());
          bp->location.file_name = extract_file_name_from_path(bp->location.file_path);
          bp->location.line = line_entry.line;

          dbg_success();
          return bp;
        }
      }
    }
  }

  dbg_fail("couldn't find specified source line");
  return nullptr;
}

void print_breakpoints(Debugger * dbg) {
  u32 count = 0;
  For (dbg->breakpoints) {
    if (it->location.file_name) {
      printf("Breakpoint #%d at %s:%ld (address 0x%lx); enabled: %d\n", count++, it->location.file_name, it->location.line, it->address, it->enabled);
    } else {
      printf("Breakpoint #%d at ?? (address 0x%lx); enabled: %d\n", count++, it->address, it->enabled);
    }
  }

  dbg_success();
}

DBG_NAMESPACE_END
