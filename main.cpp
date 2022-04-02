#include "debugger.h"

#include "imgui/imgui.h"
#include "imgui/imgui_impl_sdl.h"
#include "imgui/imgui_impl_opengl3.h"

// Build all in one translation unit. Fuck slow linker
#include "imgui/imgui.cpp"
#include "imgui/imgui_demo.cpp"
#include "imgui/imgui_draw.cpp"
#include "imgui/imgui_tables.cpp"
#include "imgui/imgui_widgets.cpp"
#include "imgui/imgui_impl_sdl.cpp"

// @Note: Include this above SDL_opengl.h is necessary to avoid names collision
#include "imgui/imgui_impl_opengl3.cpp"

#include <SDL.h>
#include <SDL_opengl.h>

#include <stdio.h>
#include <thread>
#include <mutex>
#include <unistd.h>

#include "defer.h"
#include "common.h"

struct Source_Location;
struct Debugger_GUI;


enum Debugger_Command_Type : u8 {
  DEBUG = 1,
  ATTACH,
  UNLOAD,
  SET_BREAKPOINT,
  REMOVE_BREAKPOINT,
  ENABLE_BREAKPOINT,
  DISABLE_BREAKPOINT,
  START,
  STOP,
  CONTINUE_EXECUTION,
  STEP_OVER,
  STEP_IN,
  STEP_OUT
};


struct Debugger_Debug_Arguments {
  char *executable_path = nullptr;
  char *arguments = nullptr;
};

struct Debugger_Attach_Arguments {
  u32 pid;
};

struct Debugger_Set_Breakpoint_Arguments {
  const char * filename;
  u32 line;
};

struct Debugger_Breakpoint_Arguments {
  dbg::Breakpoint * breakpoint;
};

union Debugger_Command_Arguments {
  Debugger_Debug_Arguments debug_arguments;
  Debugger_Attach_Arguments attach_arguments;
  Debugger_Set_Breakpoint_Arguments set_breakpoint_arguments;
  Debugger_Breakpoint_Arguments breakpoint_arguments;
};

struct Debugger_Command {
  Debugger_Command_Type type;
  Debugger_Command_Arguments arguments;
};

Debugger_Command Global_command = {};
bool Global_wait_for_command_finished = true;

std::mutex Global_debugger_mutex;


void send_command(Debugger_Command_Type command, Debugger_Command_Arguments arguments) {
  if (Global_wait_for_command_finished) {
    Global_command.type = command;
    Global_command.arguments = arguments;

    Global_wait_for_command_finished = false;
  }
}

inline void send_command(Debugger_Command_Type command) {
  send_command(command, (Debugger_Command_Arguments){});
}

inline void send_command(Debugger_Command_Type command, char *executable_path, char *arguments) {
  auto args = (Debugger_Debug_Arguments){executable_path, arguments};
  send_command(command, (Debugger_Command_Arguments){.debug_arguments = args});
}

inline void send_command(Debugger_Command_Type command, u32 pid) {
  auto args = (Debugger_Attach_Arguments){pid};
  send_command(command, (Debugger_Command_Arguments){.attach_arguments = args});
}

inline void send_command(Debugger_Command_Type command, const char * filename, u32 line) {
  auto args = (Debugger_Set_Breakpoint_Arguments){filename, line};
  send_command(command, (Debugger_Command_Arguments){.set_breakpoint_arguments = args});
}

inline void send_command(Debugger_Command_Type command, dbg::Breakpoint * breakpoint) {
  auto args = (Debugger_Breakpoint_Arguments){breakpoint};
  send_command(command, (Debugger_Command_Arguments){.breakpoint_arguments = args});
}

Debugger_Command get_command() {
  auto result = Global_command;
  Global_command = (Debugger_Command){};
  return result;
}

void continue_command_thread() {
  Global_wait_for_command_finished = true;
}

#include "debugger.cpp"
#include "gui.cpp"

void debugger_loop(Debugger_GUI *debugger_gui) {
  auto &dbg = debugger_gui->d;
  while (true) {
    auto c = get_command();
    if (c.type) {
      std::lock_guard<std::mutex> lock(Global_debugger_mutex);

      switch (c.type) {
      case DEBUG:
        dbg::debug(dbg, c.arguments.debug_arguments.executable_path, c.arguments.debug_arguments.arguments);
        break;

      case ATTACH:
        dbg::attach(dbg, c.arguments.attach_arguments.pid);
        break;

      case SET_BREAKPOINT:
        dbg::set_breakpoint(dbg, c.arguments.set_breakpoint_arguments.filename, c.arguments.set_breakpoint_arguments.line);
        break;

      case REMOVE_BREAKPOINT:
        dbg::remove_breakpoint(dbg, c.arguments.breakpoint_arguments.breakpoint);
        break;

      case ENABLE_BREAKPOINT:
        dbg::enable_breakpoint(dbg, c.arguments.breakpoint_arguments.breakpoint);
        break;

      case DISABLE_BREAKPOINT:
        dbg::disable_breakpoint(dbg, c.arguments.breakpoint_arguments.breakpoint);
        break;
        
      case START:              dbg::start(dbg); break;
      case STOP:               dbg::stop(dbg); break;
      case UNLOAD:             dbg::unload(dbg); break;
      case CONTINUE_EXECUTION: dbg::continue_execution(dbg); break;
      case STEP_OVER:          dbg::step_over(dbg); break;
      case STEP_IN:            dbg::step_in(dbg); break;
      case STEP_OUT:           dbg::step_out(dbg); break;

      default: assert(false && "Unknown debugger command");
      }

      update_in_debugger_thread(debugger_gui);
    }

    if (!Global_wait_for_command_finished) continue_command_thread();

    sleep(0.001); // @Hack: Think of a better solution
  }
}


void BeginFrame() {
  ImGui_ImplOpenGL3_NewFrame();
  ImGui_ImplSDL2_NewFrame();
  ImGui::NewFrame();
}

void EndFrame(SDL_Window *window, ImGuiIO io) {
  static ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

  ImGui::Render();
  glViewport(0, 0, (s32)io.DisplaySize.x, (s32)io.DisplaySize.y);
  glClearColor(clear_color.x * clear_color.w, clear_color.y * clear_color.w, clear_color.z * clear_color.w, clear_color.w);
  glClear(GL_COLOR_BUFFER_BIT);
  ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

  SDL_GL_SwapWindow(window);
}

s32 main() {
  // Initialize debugger
  Debugger_GUI debugger_gui;
  debugger_gui.init_debugger();

  std::thread gui_thread([&]() {
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

    ImGui_ImplSDL2_InitForOpenGL(window, gl_context);
    ImGui_ImplOpenGL3_Init(glsl_version);

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

      BeginFrame();

      {
        std::lock_guard<std::mutex> lock(Global_debugger_mutex);

        // ImGui::ShowDemoWindow(nullptr);
        debugger_gui.draw();

        debugger_gui.update();
      }

      EndFrame(window, io);
    }

    // Clean up
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();

    SDL_GL_DeleteContext(gl_context);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
  });

  debugger_loop(&debugger_gui);

  gui_thread.join();
  debugger_gui.deinit_debugger();
}
