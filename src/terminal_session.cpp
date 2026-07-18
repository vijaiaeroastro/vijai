#include "horcrux/terminal_session.hpp"

#include <vterm.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstring>
#include <cstdint>
#include <string_view>
#include <utility>

#ifdef _WIN32
#include <windows.h>
#else
#ifdef __APPLE__
#include <util.h>
#else
#include <pty.h>
#endif
#include <fcntl.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace horcrux {
namespace {

#ifdef _WIN32
std::string windows_error(const char* action) {
  return std::string(action) + " (Windows error " + std::to_string(GetLastError()) + ")";
}
#endif

void append_utf8(std::string& output, const std::uint32_t code_point) {
  if (code_point == 0U) return;
  if (code_point <= 0x7fU) {
    output.push_back(static_cast<char>(code_point));
  } else if (code_point <= 0x7ffU) {
    output.push_back(static_cast<char>(0xc0U | (code_point >> 6U)));
    output.push_back(static_cast<char>(0x80U | (code_point & 0x3fU)));
  } else if (code_point <= 0xffffU) {
    output.push_back(static_cast<char>(0xe0U | (code_point >> 12U)));
    output.push_back(static_cast<char>(0x80U | ((code_point >> 6U) & 0x3fU)));
    output.push_back(static_cast<char>(0x80U | (code_point & 0x3fU)));
  } else {
    output.push_back(static_cast<char>(0xf0U | (code_point >> 18U)));
    output.push_back(static_cast<char>(0x80U | ((code_point >> 12U) & 0x3fU)));
    output.push_back(static_cast<char>(0x80U | ((code_point >> 6U) & 0x3fU)));
    output.push_back(static_cast<char>(0x80U | (code_point & 0x3fU)));
  }
}

}  // namespace

class TerminalSession::Impl {
 public:
  ~Impl() { stop(); }

  bool start(const std::filesystem::path& working_directory, const int requested_rows,
             const int requested_columns, std::string& start_error) {
    stop();
    error_message.clear();
    rows = std::max(1, requested_rows);
    columns = std::max(1, requested_columns);
    terminal = vterm_new(rows, columns);
    if (terminal == nullptr) {
      start_error = "could not create terminal emulator";
      return false;
    }
    vterm_set_utf8(terminal, 1);
    screen = vterm_obtain_screen(terminal);
    vterm_screen_enable_altscreen(screen, 1);
    vterm_screen_reset(screen, 1);

#ifdef _WIN32
    SECURITY_ATTRIBUTES attributes{};
    attributes.nLength = sizeof(attributes);
    attributes.bInheritHandle = FALSE;
    if (!CreatePipe(&pseudo_input_read, &pseudo_input_write, &attributes, 0) ||
        !CreatePipe(&pseudo_output_read, &pseudo_output_write, &attributes, 0)) {
      start_error = windows_error("could not create ConPTY pipes");
      stop();
      return false;
    }
    if (FAILED(CreatePseudoConsole(COORD{static_cast<SHORT>(columns), static_cast<SHORT>(rows)},
                                   pseudo_input_read, pseudo_output_write, 0, &pseudo_console))) {
      start_error = windows_error("could not create ConPTY");
      stop();
      return false;
    }
    CloseHandle(pseudo_input_read);
    pseudo_input_read = nullptr;
    CloseHandle(pseudo_output_write);
    pseudo_output_write = nullptr;

    SIZE_T attributes_size = 0U;
    (void)InitializeProcThreadAttributeList(nullptr, 1, 0, &attributes_size);
    startup_attributes.resize(attributes_size);
    auto* attribute_list = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(startup_attributes.data());
    if (!InitializeProcThreadAttributeList(attribute_list, 1, 0, &attributes_size) ||
        !UpdateProcThreadAttribute(attribute_list, 0, PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE,
                                   pseudo_console, sizeof(pseudo_console), nullptr, nullptr)) {
      start_error = windows_error("could not configure ConPTY process");
      stop();
      return false;
    }
    startup_attributes_initialized = true;
    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);
    startup.lpAttributeList = attribute_list;
    PROCESS_INFORMATION process_info{};
    std::wstring command = L"bash.exe -i";
    const auto directory = working_directory.wstring();
    if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE,
                        EXTENDED_STARTUPINFO_PRESENT, nullptr, directory.c_str(),
                        &startup.StartupInfo, &process_info)) {
      start_error = windows_error("could not start bash.exe; add Git Bash to PATH");
      stop();
      return false;
    }
    process_handle = process_info.hProcess;
    CloseHandle(process_info.hThread);
    active = true;
    send("export TERM=xterm-256color\r");
    return true;
#else
    struct winsize size {};
    size.ws_row = static_cast<unsigned short>(rows);
    size.ws_col = static_cast<unsigned short>(columns);
    child = forkpty(&master, nullptr, nullptr, &size);
    if (child < 0) {
      start_error = "could not create shell PTY: " + std::string(std::strerror(errno));
      vterm_free(terminal);
      terminal = nullptr;
      screen = nullptr;
      return false;
    }
    if (child == 0) {
      if (chdir(working_directory.c_str()) != 0) _exit(127);
      setenv("TERM", "xterm-256color", 1);
      execlp("bash", "bash", "-i", static_cast<char*>(nullptr));
      _exit(127);
    }
    const auto flags = fcntl(master, F_GETFL, 0);
    if (flags >= 0) (void)fcntl(master, F_SETFL, flags | O_NONBLOCK);
    active = true;
    return true;
#endif
  }

  void stop() {
#ifdef _WIN32
    if (process_handle != nullptr) {
      (void)TerminateProcess(process_handle, 0);
      (void)WaitForSingleObject(process_handle, 1000);
      CloseHandle(process_handle);
      process_handle = nullptr;
    }
    if (pseudo_console != nullptr) {
      ClosePseudoConsole(pseudo_console);
      pseudo_console = nullptr;
    }
    for (HANDLE* handle : {&pseudo_input_read, &pseudo_input_write, &pseudo_output_read,
                           &pseudo_output_write}) {
      if (*handle != nullptr) {
        CloseHandle(*handle);
        *handle = nullptr;
      }
    }
    if (startup_attributes_initialized) {
      DeleteProcThreadAttributeList(
          reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(startup_attributes.data()));
      startup_attributes_initialized = false;
    }
    startup_attributes.clear();
#else
    if (child > 0) {
      (void)kill(child, SIGHUP);
      int wait_status = 0;
      bool exited = false;
      for (int attempt = 0; attempt < 50; ++attempt) {
        const auto waited = waitpid(child, &wait_status, WNOHANG);
        if (waited == child || waited < 0) {
          exited = true;
          break;
        }
        (void)usleep(10'000);
      }
      if (!exited) {
        (void)kill(child, SIGKILL);
        (void)waitpid(child, &wait_status, 0);
      }
      child = -1;
    }
    if (master >= 0) {
      (void)close(master);
      master = -1;
    }
#endif
    active = false;
    if (terminal != nullptr) vterm_free(terminal);
    terminal = nullptr;
    screen = nullptr;
  }

  void resize(const int requested_rows, const int requested_columns) {
    if (terminal == nullptr) return;
    rows = std::max(1, requested_rows);
    columns = std::max(1, requested_columns);
    vterm_set_size(terminal, rows, columns);
#ifdef _WIN32
    if (pseudo_console != nullptr) {
      (void)ResizePseudoConsole(pseudo_console,
                                COORD{static_cast<SHORT>(columns), static_cast<SHORT>(rows)});
    }
#else
    if (master >= 0) {
      struct winsize size {};
      size.ws_row = static_cast<unsigned short>(rows);
      size.ws_col = static_cast<unsigned short>(columns);
      (void)ioctl(master, TIOCSWINSZ, &size);
    }
#endif
  }

  void poll() {
#ifdef _WIN32
    if (!active || pseudo_output_read == nullptr || terminal == nullptr) return;
    DWORD available = 0U;
    while (PeekNamedPipe(pseudo_output_read, nullptr, 0, nullptr, &available, nullptr) && available > 0U) {
      std::array<char, 8192> bytes{};
      DWORD read_count = 0U;
      const DWORD to_read = std::min<DWORD>(available, static_cast<DWORD>(bytes.size()));
      if (!ReadFile(pseudo_output_read, bytes.data(), to_read, &read_count, nullptr) || read_count == 0U) {
        error_message = windows_error("shell ConPTY read failed");
        active = false;
        break;
      }
      (void)vterm_input_write(terminal, bytes.data(), read_count);
      available = 0U;
    }
    if (process_handle != nullptr && WaitForSingleObject(process_handle, 0) == WAIT_OBJECT_0) active = false;
    vterm_screen_flush_damage(screen);
#else
    if (!active || master < 0 || terminal == nullptr) return;
    std::array<char, 8192> bytes{};
    for (;;) {
      const auto read_count = read(master, bytes.data(), bytes.size());
      if (read_count > 0) {
        (void)vterm_input_write(terminal, bytes.data(), static_cast<std::size_t>(read_count));
        continue;
      }
      if (read_count == 0 || (errno != EAGAIN && errno != EWOULDBLOCK)) {
        active = false;
        if (read_count < 0) error_message = "shell PTY read failed: " + std::string(std::strerror(errno));
      }
      break;
    }
    vterm_screen_flush_damage(screen);
#endif
  }

  void send(const std::string_view text) {
#ifdef _WIN32
    if (!active || pseudo_input_write == nullptr || text.empty()) return;
    DWORD written = 0U;
    if (!WriteFile(pseudo_input_write, text.data(), static_cast<DWORD>(text.size()), &written, nullptr)) {
      error_message = windows_error("shell ConPTY write failed");
      active = false;
    }
#else
    if (!active || master < 0 || text.empty()) return;
    const auto written = write(master, text.data(), text.size());
    if (written < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
      error_message = "shell PTY write failed: " + std::string(std::strerror(errno));
      active = false;
    }
#endif
  }

  [[nodiscard]] std::vector<std::string> lines() const {
    std::vector<std::string> result;
    if (screen == nullptr) return result;
    result.reserve(static_cast<std::size_t>(rows));
    for (int row = 0; row < rows; ++row) {
      std::string line;
      line.reserve(static_cast<std::size_t>(columns));
      for (int column = 0; column < columns; ++column) {
        VTermScreenCell cell{};
        (void)vterm_screen_get_cell(screen, VTermPos{row, column}, &cell);
        if (cell.width == 0) continue;
        if (cell.chars[0] == 0U) {
          line.push_back(' ');
          continue;
        }
        for (const auto character : cell.chars) append_utf8(line, character);
      }
      while (!line.empty() && line.back() == ' ') line.pop_back();
      result.push_back(std::move(line));
    }
    return result;
  }

  [[nodiscard]] std::vector<TerminalScreenLine> styled_lines() const {
    std::vector<TerminalScreenLine> result;
    if (screen == nullptr) return result;
    result.reserve(static_cast<std::size_t>(rows));
    for (int row = 0; row < rows; ++row) {
      TerminalScreenLine line;
      line.reserve(static_cast<std::size_t>(columns));
      for (int column = 0; column < columns; ++column) {
        VTermScreenCell source{};
        (void)vterm_screen_get_cell(screen, VTermPos{row, column}, &source);
        TerminalCell cell;
        if (source.width != 0) {
          for (const auto character : source.chars) append_utf8(cell.text, character);
          if (cell.text.empty()) cell.text = " ";
        }
        const auto colour = [this](VTermColor value) -> std::optional<TerminalColour> {
          if (VTERM_COLOR_IS_DEFAULT_FG(&value) || VTERM_COLOR_IS_DEFAULT_BG(&value)) {
            return std::nullopt;
          }
          vterm_screen_convert_color_to_rgb(screen, &value);
          return TerminalColour{.red = value.rgb.red, .green = value.rgb.green,
                                .blue = value.rgb.blue};
        };
        cell.foreground = colour(source.fg);
        cell.background = colour(source.bg);
        cell.bold = source.attrs.bold != 0U;
        cell.italic = source.attrs.italic != 0U;
        cell.underlined = source.attrs.underline != 0U;
        line.push_back(std::move(cell));
      }
      while (!line.empty() && line.back().text == " " && !line.back().foreground &&
             !line.back().background && !line.back().bold && !line.back().italic &&
             !line.back().underlined) {
        line.pop_back();
      }
      result.push_back(std::move(line));
    }
    return result;
  }

  VTerm* terminal{nullptr};
  VTermScreen* screen{nullptr};
  int rows{1};
  int columns{1};
  bool active{false};
  std::string error_message;
#ifndef _WIN32
  int master{-1};
  pid_t child{-1};
#else
  HPCON pseudo_console{nullptr};
  HANDLE pseudo_input_read{nullptr};
  HANDLE pseudo_input_write{nullptr};
  HANDLE pseudo_output_read{nullptr};
  HANDLE pseudo_output_write{nullptr};
  HANDLE process_handle{nullptr};
  std::vector<std::byte> startup_attributes;
  bool startup_attributes_initialized{false};
#endif
};

TerminalSession::TerminalSession() : impl_(std::make_unique<Impl>()) {}
TerminalSession::~TerminalSession() = default;

bool TerminalSession::start(const std::filesystem::path& working_directory, const int rows,
                            const int columns, std::string& error) {
  error.clear();
  const bool started = impl_->start(working_directory, rows, columns, error);
  if (!started) impl_->error_message = error;
  return started;
}

void TerminalSession::stop() { impl_->stop(); }
void TerminalSession::resize(const int rows, const int columns) { impl_->resize(rows, columns); }
void TerminalSession::poll() { impl_->poll(); }
void TerminalSession::send_text(const std::string& text) { impl_->send(text); }

void TerminalSession::send_key(const TerminalKey key) {
  switch (key) {
    case TerminalKey::up: impl_->send("\x1b[A"); break;
    case TerminalKey::down: impl_->send("\x1b[B"); break;
    case TerminalKey::left: impl_->send("\x1b[D"); break;
    case TerminalKey::right: impl_->send("\x1b[C"); break;
    case TerminalKey::home: impl_->send("\x1b[H"); break;
    case TerminalKey::end: impl_->send("\x1b[F"); break;
    case TerminalKey::page_up: impl_->send("\x1b[5~"); break;
    case TerminalKey::page_down: impl_->send("\x1b[6~"); break;
    case TerminalKey::backspace: impl_->send("\x7f"); break;
    case TerminalKey::escape: impl_->send("\x1b"); break;
  }
}

bool TerminalSession::running() const noexcept { return impl_->active; }

std::optional<TerminalCursor> TerminalSession::cursor() const {
  if (impl_->terminal == nullptr) return std::nullopt;
  VTermPos position{};
  vterm_state_get_cursorpos(vterm_obtain_state(impl_->terminal), &position);
  return TerminalCursor{.row = position.row, .column = position.col};
}
const std::string& TerminalSession::error() const noexcept { return impl_->error_message; }
std::vector<std::string> TerminalSession::screen_lines() const { return impl_->lines(); }
std::vector<TerminalScreenLine> TerminalSession::screen() const { return impl_->styled_lines(); }

}  // namespace horcrux
