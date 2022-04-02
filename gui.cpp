
struct Debugger_GUI {
  dbg::Debugger debugger;
  dbg::Debugger * d = &debugger;

  dbg::Source_Location m_source_location;
  dbg::Source_Location m_last_source_location;

  Array<dbg::Frame> m_stack_trace;
  Array<dbg::Variable> m_local_variables;
  Array<u64> m_register_values;

  dbg::Debugger_State m_last_debugger_state = d->state;

  void init_debugger() {
    init(d);
    d->verbose = true;
    d->autorestart_enabled = true;
  }

  void deinit_debugger() {
    deinit(d);
  }

  void show_code_panel();
  void show_breakpoints_panel();
  void show_variables_panel();
  void show_stack_panel();
  void show_register_panel();
  void show_symbols_panel();
  void show_memory_panel();
  void show_debugger_window();

  void update();
  void draw() { show_debugger_window(); }
};

void Debugger_GUI::show_code_panel() {
  if (ImGui::Begin("Code", NULL, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
    if (ImGui::BeginTabBar("Files")) {

      if (d->state == dbg::Debugger_State::NOT_LOADED) {
        ImGui::EndTabBar();
        ImGui::End();
        return;
      }

      auto sources = dbg::get_updated_sources(d);

      if (sources.count <= 0) {
        ImGui::EndTabBar();
        ImGui::End();
        return;
      }

      Array<bool> is_tab_open;
      is_tab_open.init(sources.count);

      For_it (sources, source) {
        ImGui::PushID(source.file_path);
        if (ImGui::BeginTabItem(source.file_name)) {
          ImGui::BeginChild("##file_content"); // Child for content scrollbar

          For_Range (1, source.lines.count + 1, line_number) {
            char line_number_buf[128];
            sprintf(line_number_buf, "%d", line_number);

            auto line_end = source.lines[line_number - 1];
            while (*line_end != '\n' && *line_end != '\0') {
              line_end++;
            }

            u64 line_length = line_end - source.lines[line_number - 1];

            // TODO: Make the search for breakpoint by Source_Location
            bool breakpoint_exists_on_the_line = false;
            dbg::Breakpoint * existing_breakpoint = nullptr;
            For (d->breakpoints) {
              if (strcmp(it->location.file_path, source.file_path) == 0) {
                if (it->location.line == line_number) {
                  breakpoint_exists_on_the_line = true;
                  existing_breakpoint = it;
                  break;
                }
              }
            }
            
            if (ImGui::Button(line_number_buf)) {
              if (!breakpoint_exists_on_the_line) {
                send_command(SET_BREAKPOINT, source.file_name, line_number);
              } else {
                send_command(REMOVE_BREAKPOINT, existing_breakpoint);
              }
            }

            ImGui::SameLine();

            // Breakpoint markers
            const ImVec2 p = ImGui::GetCursorScreenPos();
            const ImU32 bk_col = ImColor(ImVec4(1.0f, 0.2f, 0.2f, 1.0f));
            const ImU32 bk_col_dis = ImColor(ImVec4(1.0f, 0.2f, 0.2f, 0.5f));
            const u32 bk_sz = 6.0f;
            ImDrawList* draw_list = ImGui::GetWindowDrawList();
            if (breakpoint_exists_on_the_line) {
              if (existing_breakpoint->enabled) {
                draw_list->AddCircleFilled(ImVec2(p.x + bk_sz*0.5f, p.y + bk_sz*1.5f), bk_sz, bk_col);
              } else {
                draw_list->AddCircleFilled(ImVec2(p.x + bk_sz*0.5f, p.y + bk_sz*1.5f), bk_sz, bk_col_dis);
              }
            }

            // PC marker
            if (d->state == dbg::Debugger_State::RUNNING) {
              auto pc_location = m_source_location;
              if (pc_location.file_path) {
                if (strcmp(pc_location.file_path, source.file_path) == 0 && pc_location.line == line_number) {
                  const ImU32 pc_col = ImColor(ImVec4(1.0f, 1.0f, 0.2f, 1.0f));
                  const float32 pc_th = 4.0f;
                  draw_list->AddLine(ImVec2(p.x - bk_sz*0.5f, p.y + bk_sz*1.5f), ImVec2(p.x + bk_sz*1.5f, p.y + bk_sz*1.5f), pc_col, pc_th);
                }
              }
            }

            ImGui::SetCursorPos({ImGui::GetCursorPosX() + bk_sz*2.0f,
                                 ImGui::GetCursorPosY()});

            ImGui::Text("%.*s", line_length, source.lines[line_number - 1]);
          }

          ImGui::EndChild();
          ImGui::EndTabItem();
        }

        ImGui::PopID();
      }
    }
    ImGui::EndTabBar();
  }
  ImGui::End();
}

void Debugger_GUI::show_breakpoints_panel() {
  if (ImGui::Begin("Breakpoints")) {
    if (ImGui::BeginTable("##breakpoints_table", 4)) {
      // Table header
      ImGui::TableNextColumn(); ImGui::Text("Enabled");
      ImGui::TableNextColumn(); ImGui::Text("Address");
      ImGui::TableNextColumn(); ImGui::Text("Location");
      ImGui::TableNextColumn();
      ImGui::TableNextRow();

      // Removing breakpoints marked on last iteration
      static Array<dbg::Breakpoint *> to_remove;
      if (to_remove.count == -1) {
        to_remove.init();
      }

      For (to_remove) {
        send_command(REMOVE_BREAKPOINT, it);
      }

      // Breakpoints removed, so clear the list
      to_remove.reset();

      For (d->breakpoints) {
        bool checkbox_enabled = it->enabled;
        char checkbox_name[128];
        sprintf(checkbox_name, "##breakpoint_%lu", it->address);

        char remove_button_name[128];
        sprintf(remove_button_name, "Remove##%lu", it->address);

        ImGui::TableNextColumn(); ImGui::Checkbox(checkbox_name, &checkbox_enabled);
        ImGui::TableNextColumn(); ImGui::Text("0x%lx", (void *)it->address);
        ImGui::TableNextColumn(); ImGui::Text("%s:%lu", it->location.file_name, it->location.line);
        ImGui::TableNextColumn();

        if (ImGui::Button(remove_button_name)) {
          to_remove.add(it);
        }

        // Change breakpoint state
        if (checkbox_enabled != it->enabled) {
          if (checkbox_enabled) {
            send_command(ENABLE_BREAKPOINT, it);
          } else {
            send_command(DISABLE_BREAKPOINT, it);
          }
        }

        ImGui::TableNextRow();
      }
    }
    ImGui::EndTable();
  }
  ImGui::End();
}

void Debugger_GUI::show_variables_panel() {
  if (ImGui::Begin("Local variables")) {
    if (ImGui::BeginTable("##variables_table", 3)) {
      // Header
      ImGui::TableNextColumn(); ImGui::Text("Name");
      ImGui::TableNextColumn(); ImGui::Text("Value");
      ImGui::TableNextColumn(); ImGui::Text("Location");
      ImGui::TableNextRow();

      For (m_local_variables) {
        ImGui::TableNextColumn(); ImGui::Text("%s", it.name);
        ImGui::TableNextColumn(); ImGui::Text("%d", it.value);
        ImGui::TableNextColumn();

        switch (it.location) {
        case dbg::Variable_Location::REGISTER:
          ImGui::Text("In register %d", dbg::register_to_string(it.reg));
          break;
        case dbg::Variable_Location::MEMORY:
          ImGui::Text("In memory at 0x%lx", it.address);
          break;
        }

        ImGui::TableNextRow();
      }
    }
    ImGui::EndTable();
  }
  ImGui::End();
}

void Debugger_GUI::show_stack_panel() {
  if (ImGui::Begin("Stack trace")) {
    if (ImGui::BeginTable("##stack_table", 4)) {

      // Table header
      ImGui::TableNextColumn(); ImGui::Text("#");
      ImGui::TableNextColumn(); ImGui::Text("Function name");
      ImGui::TableNextColumn(); ImGui::Text("Location");
      ImGui::TableNextColumn(); ImGui::Text("Address");
      ImGui::TableNextRow();

      u32 frame_id = 0;
      For (m_stack_trace) {
        ImGui::TableNextColumn(); ImGui::Text("%d", frame_id);
        ImGui::TableNextColumn(); ImGui::Text("%s", it.function_name);
        ImGui::TableNextColumn(); ImGui::Text("%s:%d", it.location.file_name, it.location.line);
        ImGui::TableNextColumn(); ImGui::Text("0x%lx", it.address);
        ImGui::TableNextRow();

        frame_id++;
      }

    }
    ImGui::EndTable();
  }
  ImGui::End();
}

void Debugger_GUI::show_register_panel() {
  if (ImGui::Begin("Registers")) {
    if (ImGui::BeginTable("##register_table", 2)) {

      // Table header
      ImGui::TableNextColumn(); ImGui::Text("Register name");
      ImGui::TableNextColumn(); ImGui::Text("Value");
      ImGui::TableNextRow();

      if (d->state == dbg::Debugger_State::RUNNING) {
        For_Pointer (m_register_values) {
          auto reg = (dbg::Register)(it - m_register_values.data);

          ImGui::TableNextColumn(); ImGui::Text("%s", register_to_string(reg));
          ImGui::TableNextColumn(); ImGui::Text("0x%lx", *it);
          ImGui::TableNextRow();
        }
      }

    }
    ImGui::EndTable();
  }
  ImGui::End();
}

void Debugger_GUI::show_symbols_panel() {
  if (ImGui::Begin("Symbols")) {
    static char symbol_query[256];
    auto input_flags = ImGuiInputTextFlags_EnterReturnsTrue;
    bool query_entered = ImGui::InputTextWithHint("##symbol_query", "symbol query", symbol_query, IM_ARRAYSIZE(symbol_query), input_flags);

    ImGui::SameLine();

    bool search_button_pressed = ImGui::Button("Search");

    static Array<dbg::Symbol> found_symbols;
    if ((query_entered || search_button_pressed) && strlen(symbol_query) > 0) {
      dbg::lookup_symbol(d, symbol_query, &found_symbols);
    }

    if (ImGui::BeginTable("##symbols_table", 3)) {

      // Table header
      ImGui::TableNextColumn(); ImGui::Text("Symbol name");
      ImGui::TableNextColumn(); ImGui::Text("Type");
      ImGui::TableNextColumn(); ImGui::Text("Address");
      ImGui::TableNextRow();

      For (found_symbols) {
        ImGui::TableNextColumn(); ImGui::Text("%s", it.name);
        ImGui::TableNextColumn(); ImGui::Text("%s", dbg::to_string(it.type));
        ImGui::TableNextColumn(); ImGui::Text("0x%lx", it.address);
        ImGui::TableNextRow();
      }
    }
    ImGui::EndTable();
  }
  ImGui::End();
}

void Debugger_GUI::show_memory_panel() { }

void Debugger_GUI::show_debugger_window() {
  ImGui::DockSpaceOverViewport(ImGui::GetMainViewport());

  if (ImGui::BeginMainMenuBar()) {
    if (ImGui::BeginMenu("File")) {
      static s32 mode_idx = 0;
      ImGui::AlignTextToFramePadding();
      ImGui::Text("Choose mode"); ImGui::SameLine();
      ImGui::Combo("##choose_mode", &mode_idx, "Debug program\0Attach to pid\0");
      switch (mode_idx) {
      case 0: {
        ImGuiInputTextFlags input_flags = ImGuiInputTextFlags_EnterReturnsTrue;
        bool is_debugger_loaded = (d->state != dbg::Debugger_State::NOT_LOADED);
        if (is_debugger_loaded) {
          input_flags |= ImGuiInputTextFlags_ReadOnly;
        }

        ImGui::AlignTextToFramePadding();
        ImGui::Text("Program"); ImGui::SameLine();
        static char debug_path[256] = "./debugee";
        if (ImGui::InputTextWithHint("##program", "program path", debug_path, IM_ARRAYSIZE(debug_path), input_flags)) {
        }

        ImGui::AlignTextToFramePadding();
        ImGui::Text("Arguments"); ImGui::SameLine();
        static char debug_arguments[256] = "";
        if (ImGui::InputTextWithHint("##arguments", "program arguments", debug_arguments, IM_ARRAYSIZE(debug_arguments), input_flags)) {
        }

        if (ImGui::MenuItem("Load debug session", NULL, false, d->state == dbg::Debugger_State::NOT_LOADED)) {
          send_command(DEBUG, debug_path, debug_arguments);
        }

        if (ImGui::MenuItem("Unload debug session", NULL, false, d->state == dbg::Debugger_State::LOADED)) {
          if (d->state == dbg::Debugger_State::RUNNING) {
            send_command(STOP);
          }
          send_command(UNLOAD);
        }
        break;
      }
      case 1: {
        ImGui::AlignTextToFramePadding();
        ImGui::Text("Process PID"); ImGui::SameLine();

        static s32 pid = 0;
        ImGuiInputTextFlags input_flags = ImGuiInputTextFlags_None;

        bool is_debugger_loaded = (d->state != dbg::Debugger_State::NOT_LOADED);
        if (is_debugger_loaded) {
          input_flags |= ImGuiInputTextFlags_ReadOnly;
        }

        if (ImGui::InputScalar("##pid", ImGuiDataType_U32, &pid, NULL, NULL, "%u", input_flags)) {
        }

        if (ImGui::MenuItem("Attach to a process", NULL, false, d->state == dbg::Debugger_State::NOT_LOADED)) {
          send_command(ATTACH, (u32)pid);
        }

        if (ImGui::MenuItem("Detach from a process", NULL, false, d->state == dbg::Debugger_State::LOADED)) {
          if (d->state == dbg::Debugger_State::RUNNING) {
            send_command(STOP);
          }
          if (d->state == dbg::Debugger_State::LOADED) {
            send_command(UNLOAD);
          }
        }
        break;
      }
      }

      ImGui::MenuItem("Exit debugger");

      ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Debug")) {
      bool is_debugger_running = (d->state == dbg::Debugger_State::RUNNING);
      if (ImGui::MenuItem("Start/Continue", "F5", false, d->state != dbg::Debugger_State::NOT_LOADED)) {
        if (is_debugger_running) {
          send_command(CONTINUE_EXECUTION);
        } else {
          send_command(START);
        }
      }

      if (ImGui::MenuItem("Stop", "Shift+F5", false, is_debugger_running)) {
        send_command(STOP);
      }

      if (ImGui::MenuItem("Step over", "F10", false, is_debugger_running)) {
        send_command(STEP_OVER);
      }

      if (ImGui::MenuItem("Step in", "F11", false, is_debugger_running)) {
        send_command(STEP_IN);
      }

      if (ImGui::MenuItem("Step out", "Shift+F11", false, is_debugger_running)) {
        send_command(STEP_OUT);
      }

      ImGui::EndMenu();
    }

    ImGui::EndMainMenuBar();
  }

  show_code_panel();
  show_breakpoints_panel();
  show_variables_panel();
  show_stack_panel();
  show_register_panel();
  show_symbols_panel();
}

// @Note: Running this function in the same thread as the debugger because
//        functions calling ptrace require to be called from the same thread.
void update_in_debugger_thread(Debugger_GUI *debugger_gui) {
  auto &d = debugger_gui->d;

  bool is_debugger_running = (d->state == dbg::Debugger_State::RUNNING);

  if (is_debugger_running) {
    if (debugger_gui->m_last_debugger_state != dbg::Debugger_State::RUNNING) {
      debugger_gui->m_last_source_location = debugger_gui->m_source_location = dbg::get_source_location(d);
    } else {
      debugger_gui->m_last_source_location = debugger_gui->m_source_location;
      debugger_gui->m_source_location = dbg::get_source_location(d);
    }

    dbg::get_variables(d, &debugger_gui->m_local_variables);
    dbg::get_stack_trace(d, &debugger_gui->m_stack_trace);
    dbg::get_registers(d, &debugger_gui->m_register_values);
  } else {
    if (debugger_gui->m_last_debugger_state == dbg::Debugger_State::RUNNING) {
      debugger_gui->m_local_variables.reset();
      debugger_gui->m_stack_trace.reset();
      debugger_gui->m_register_values.reset();
    }
  }
}

void Debugger_GUI::update() {
  bool is_debugger_running = (d->state == dbg::Debugger_State::RUNNING);

  if (!ImGui::IsKeyDown(ImGuiKey_ModShift) && ImGui::IsKeyPressed(ImGuiKey_F5)) {
    if (is_debugger_running) {
      send_command(CONTINUE_EXECUTION);
    } else {
      send_command(START);
    }
  }

  if (ImGui::IsKeyDown(ImGuiKey_ModShift) && ImGui::IsKeyPressed(ImGuiKey_F5)) {
    send_command(STOP);
  }

  if (ImGui::IsKeyPressed(ImGuiKey_F10)) {
    send_command(STEP_OVER);
  }

  if (!ImGui::IsKeyDown(ImGuiKey_ModShift) && ImGui::IsKeyPressed(ImGuiKey_F11)) {
    send_command(STEP_IN);
  }

  if (ImGui::IsKeyDown(ImGuiKey_ModShift) && ImGui::IsKeyPressed(ImGuiKey_F11)) {
    send_command(STEP_OUT);
  }

  m_last_debugger_state = d->state;
}
