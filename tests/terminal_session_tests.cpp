#include "horcrux/terminal_session.hpp"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <string>
#include <thread>

namespace {

void terminal_session_tests() {
#ifndef _WIN32
  horcrux::TerminalSession shell;
  std::string error;
  assert(shell.start(std::filesystem::current_path(), 12, 80, error));
  shell.send_text("printf 'HORCRUX_PTY_SMOKE_TEST\\n'\r");

  bool found_output = false;
  for (int attempt = 0; attempt < 100 && !found_output; ++attempt) {
    shell.poll();
    for (const auto& line : shell.screen_lines()) {
      if (line.find("HORCRUX_PTY_SMOKE_TEST") != std::string::npos) {
        found_output = true;
        break;
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  assert(found_output);
#endif
}

struct RunTerminalSessionTests {
  RunTerminalSessionTests() { terminal_session_tests(); }
};

RunTerminalSessionTests run_terminal_session_tests;

}  // namespace
