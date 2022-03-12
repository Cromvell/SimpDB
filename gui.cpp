#include "debugger.h"

#include "imgui/imgui.h"
#include "imgui/imgui_impl_sdl.h"
#include "imgui/imgui_impl_opengl3.h"

#include <SDL.h>
#include <SDL_opengl.h>

#include <stdio.h>

#include "defer.h"
#include "common.h"

// Build all in one translation unit. Fuck slow linker
#include "debugger.cpp"

#include "imgui/imgui.cpp"
#include "imgui/imgui_demo.cpp"
#include "imgui/imgui_draw.cpp"
#include "imgui/imgui_tables.cpp"
#include "imgui/imgui_widgets.cpp"
// #include "imgui/imgui_impl_opengl3.cpp" // TODO: Eleminate second TU
#include "imgui/imgui_impl_sdl.cpp"

// TODO: Refactor this to DebuggerGUI structure
// struct DebuggerGUI { }

dbg::Debugger debugger;
dbg::Debugger * d = &debugger;

dbg::Source_Location g_source_location;
dbg::Source_Location g_last_source_location;
Array<dbg::Frame> g_stack_trace;

Array<dbg::Variable> g_local_variables;

void init_debugger(dbg::Debugger * dbg) {
  init(dbg);
  dbg->verbose = true;
}

void deinit_debugger(dbg::Debugger * dbg) {
  deinit(dbg);
}

void show_code_panel() {
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
                dbg::set_breakpoint(d, source.file_name, line_number);
              } else {
                dbg::remove_breakpoint(d, existing_breakpoint);
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
              auto pc_location = dbg::get_source_location(d);
              if (d->last_command_status == dbg::Command_Status::SUCCESS) {
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

void show_breakpoints_panel() {
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
        dbg::remove_breakpoint(d, it);
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
            dbg::enable_breakpoint(d, it);
          } else {
            dbg::disable_breakpoint(d, it);
          }
        }

        ImGui::TableNextRow();
      }
    }
    ImGui::EndTable();
  }
  ImGui::End();
}

void show_variables_panel() {
  if (ImGui::Begin("Local variables")) {
    if (ImGui::BeginTable("##variables_table", 3)) {
      // Header
      ImGui::TableNextColumn(); ImGui::Text("Name");
      ImGui::TableNextColumn(); ImGui::Text("Value");
      ImGui::TableNextColumn(); ImGui::Text("Location");
      ImGui::TableNextRow();

      For (g_local_variables) {
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

void show_stack_panel() {
  if (ImGui::Begin("Stack trace")) {
    if (ImGui::BeginTable("##stack_table", 4)) {

      // Table header
      ImGui::TableNextColumn(); ImGui::Text("#");
      ImGui::TableNextColumn(); ImGui::Text("Function name");
      ImGui::TableNextColumn(); ImGui::Text("Location");
      ImGui::TableNextColumn(); ImGui::Text("Address");
      ImGui::TableNextRow();

      u32 frame_id = 0;
      For (g_stack_trace) {
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

void show_register_panel() {
  if (ImGui::Begin("Registers")) {
    if (ImGui::BeginTable("##register_table", 2)) {

      // Table header
      ImGui::TableNextColumn(); ImGui::Text("Register name");
      ImGui::TableNextColumn(); ImGui::Text("Value");
      ImGui::TableNextRow();

      if (d->state == dbg::Debugger_State::RUNNING) {
        u32 reg_id = 0;
        while ((dbg::Register)reg_id < dbg::Register::UNKNOWN) {
          auto reg = (dbg::Register)reg_id;

          auto reg_value = dbg::read_register(d, reg);
        
          ImGui::TableNextColumn(); ImGui::Text("%s", register_to_string(reg));
          ImGui::TableNextColumn(); ImGui::Text("0x%lx", reg_value);
          ImGui::TableNextRow();

          reg_id++;
        }
      }

    }
    ImGui::EndTable();
  }
  ImGui::End();
}

void show_symbols_panel() {
  if (ImGui::Begin("Symbols")) {
    static char symbol_query[256];
    auto input_flags = ImGuiInputTextFlags_EnterReturnsTrue;
    bool query_entered = ImGui::InputTextWithHint("##symbol_query", "symbol query", symbol_query, IM_ARRAYSIZE(symbol_query), input_flags);

    ImGui::SameLine();

    bool search_button_pressed = ImGui::Button("Search");

    static Array<dbg::Symbol> found_symbols;
    if ((query_entered || search_button_pressed) && strlen(symbol_query) > 0) {
      found_symbols = dbg::lookup_symbol(d, symbol_query);
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

void show_memory_panel() { }

void show_debugger_window() {
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
          dbg::debug(d, debug_path, debug_arguments);
        }

        if (ImGui::MenuItem("Unload debug session", NULL, false, d->state == dbg::Debugger_State::LOADED)) {
          if (d->state == dbg::Debugger_State::RUNNING) {
            dbg::stop(d);
          }
          dbg::unload(d);
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
          dbg::attach(d, (u32)pid);
        }

        if (ImGui::MenuItem("Detach from a process", NULL, false, d->state == dbg::Debugger_State::LOADED)) {
          if (d->state == dbg::Debugger_State::RUNNING) {
            dbg::stop(d);
          }
          if (d->state == dbg::Debugger_State::LOADED) {
            dbg::unload(d);
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
          dbg::continue_execution(d);
        } else {
          dbg::start(d);
        }
      }

      if (ImGui::MenuItem("Stop", "Shift+F5", false, is_debugger_running)) {
        dbg::stop(d);
      }

      if (ImGui::MenuItem("Step over", "F10", false, is_debugger_running)) {
        dbg::step_over(d);
      }

      if (ImGui::MenuItem("Step in", "F11", false, is_debugger_running)) {
        dbg::step_in(d);
      }

      if (ImGui::MenuItem("Step out", "Shift+F11", false, is_debugger_running)) {
        dbg::step_out(d);
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

void debugger_update() {
  bool is_debugger_running = (d->state == dbg::Debugger_State::RUNNING);
  static dbg::Debugger_State last_debugger_state = d->state;

  if (is_debugger_running) {
    if (last_debugger_state != dbg::Debugger_State::RUNNING) {
      g_last_source_location = g_source_location = dbg::get_source_location(d);
    } else {
      g_last_source_location = g_source_location;
      g_source_location = dbg::get_source_location(d);

      g_local_variables = dbg::get_variables(d);
      g_stack_trace = dbg::get_stack_trace(d);
    }
  }

  if (!ImGui::IsKeyDown(ImGuiKey_ModShift) && ImGui::IsKeyPressed(ImGuiKey_F5)) {
    if (is_debugger_running) {
      dbg::continue_execution(d);
    } else {
      dbg::start(d);
    }
  }

  if (ImGui::IsKeyDown(ImGuiKey_ModShift) && ImGui::IsKeyPressed(ImGuiKey_F5)) {
    dbg::stop(d);
  }

  if (ImGui::IsKeyPressed(ImGuiKey_F10)) {
    dbg::step_over(d);
  }

  if (!ImGui::IsKeyDown(ImGuiKey_ModShift) && ImGui::IsKeyPressed(ImGuiKey_F11)) {
    dbg::step_in(d);
  }

  if (ImGui::IsKeyDown(ImGuiKey_ModShift) && ImGui::IsKeyPressed(ImGuiKey_F11)) {
    dbg::step_out(d);
  }

  last_debugger_state = d->state;
}

void debugger_loop_cleanup() {
  g_local_variables.deinit();
}

s32 main() {
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
    printf("Error: %s\n", SDL_GetError());
    return -1;
  }

  const char* glsl_version = "#version 130";
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);

  // Create window with graphical context
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
  SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
  SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
  SDL_WindowFlags window_flags = (SDL_WindowFlags)(SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
  SDL_Window* window = SDL_CreateWindow("SimpDBG", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 720, window_flags);
  SDL_GLContext gl_context = SDL_GL_CreateContext(window);
  SDL_GL_MakeCurrent(window, gl_context);
  SDL_GL_SetSwapInterval(1); // Enable vsync

  // Setup Deer ImGui context
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO(); (void)io;

  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

  ImGui::StyleColorsDark();
  ImGui::StyleColorsClassic();

  // ImGuiStyle& style = ImGui::GetStyle();
  // style.FramePadding.y = 4;

  ImGui_ImplSDL2_InitForOpenGL(window, gl_context);
  ImGui_ImplOpenGL3_Init(glsl_version);

  ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

  init_debugger(&debugger);

  bool done = false;
  while (!done) {
    // Poll and handle events
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      ImGui_ImplSDL2_ProcessEvent(&event);
      if (event.type == SDL_QUIT)
        done = true;
      if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_CLOSE && event.window.windowID == SDL_GetWindowID(window))
        done = true;
    }

    debugger_update();

    // Start the Dear ImGui frame
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();

    // ImGui::ShowDemoWindow(nullptr);
    show_debugger_window();

    ImGui::Render();
    glViewport(0, 0, (s32)io.DisplaySize.x, (s32)io.DisplaySize.y);
    glClearColor(clear_color.x * clear_color.w, clear_color.y * clear_color.w, clear_color.z * clear_color.w, clear_color.w);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    SDL_GL_SwapWindow(window);

    debugger_loop_cleanup();
  }

  // Clean up
  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplSDL2_Shutdown();
  ImGui::DestroyContext();

  SDL_GL_DeleteContext(gl_context);
  SDL_DestroyWindow(window);
  SDL_Quit();

  deinit_debugger(&debugger);
}
