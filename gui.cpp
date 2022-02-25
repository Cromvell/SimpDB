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

dbg::Debugger debugger;
dbg::Debugger * d = &debugger;

void init_debugger(dbg::Debugger * dbg) {
  init(dbg);
  dbg->verbose = true;
}

void deinit_debugger(dbg::Debugger * dbg) {
  deinit(dbg);
}

void show_code_panel() {
  if (ImGui::Begin("Code")) {
    if (ImGui::BeginTabBar("Files")) {
      char *filenames[] = {
                           "debugee.cpp",
                           "debugee2.cpp",
      };
      bool is_tab_open[] = {
                            true,
                            true,
      };

      // Array<dbg::Source_File> sources = dbg::get_sources(dbg);

      // For (sources) {
      for (s32 i = 0; i < 4; i++) {
        bool visible = ImGui::BeginTabItem(filenames[i], is_tab_open, 0);

        if (visible) {
          ImGui::PushID(&(filenames[i]));
          // ImGui::PushID(it.filepath);

          u32 line_number = 0;
          // For_Count (it.line_count) {
          //   ImGui::Text(it.lines[line_number], line_number++));
          // }
          
          ImGui::PopID();
          ImGui::EndTabItem();
        }
      }

      ImGui::EndTabBar();
    }
    ImGui::End();
  }
}

void show_breakpoints_panel() { }

void show_register_panel() { }

void show_stack_panel() { }

void show_variables_panel() { }

// ???
void show_symbols_panel() { }
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
        bool is_debugger_running = (d->state == dbg::Debugger_State::RUNNING);
        if (is_debugger_running) {
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

        if (ImGui::MenuItem("Start debug session", NULL, false, !is_debugger_running)) {
          dbg::debug(d, debug_path, debug_arguments);
          dbg::start(d);
        }

        if (ImGui::MenuItem("Stop debug session", NULL, false, is_debugger_running)) {
          dbg::stop(d);
        }
        break;
      }
      case 1: {
        ImGui::AlignTextToFramePadding();
        ImGui::Text("Process PID"); ImGui::SameLine();

        static s32 pid = 0;
        ImGuiInputTextFlags input_flags = ImGuiInputTextFlags_EnterReturnsTrue;

        bool is_debugger_running = (d->state == dbg::Debugger_State::RUNNING);
        if (is_debugger_running) {
          input_flags |= ImGuiInputTextFlags_ReadOnly;
        }

        if (ImGui::InputScalar("##pid", ImGuiDataType_U32, &pid, NULL, NULL, "%u", input_flags)) {
        }

        if (ImGui::MenuItem("Attach to a process", NULL, false, !is_debugger_running)) {
          dbg::attach(d, (u32)pid);
          dbg::start(d);
        }

        if (ImGui::MenuItem("Detach from a process", NULL, false, is_debugger_running)) {
          dbg::stop(d);
        }

        break;
      }
      }

      ImGui::MenuItem("Exit debugger");

      ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Debug")) {
      bool is_debugger_running = (d->state == dbg::Debugger_State::RUNNING);
      if (ImGui::MenuItem("Continue", "F5", false, is_debugger_running)) {
        dbg::continue_execution(d);
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

  // show_code_panel();
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
