#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace horcrux {

enum class TerminalKey { up, down, left, right, home, end, page_up, page_down, backspace, escape };

struct TerminalCursor {
  int row{0};
  int column{0};
};

struct TerminalColour {
  std::uint8_t red{0};
  std::uint8_t green{0};
  std::uint8_t blue{0};
};

struct TerminalCell {
  std::string text;
  std::optional<TerminalColour> foreground;
  std::optional<TerminalColour> background;
  bool bold{false};
  bool italic{false};
  bool underlined{false};
};

using TerminalScreenLine = std::vector<TerminalCell>;

// A persistent interactive shell backed by a platform pseudo-terminal. Output
// is interpreted by libvterm before it is exposed to the FTXUI frontend.
class TerminalSession {
 public:
  TerminalSession();
  ~TerminalSession();
  TerminalSession(const TerminalSession&) = delete;
  TerminalSession& operator=(const TerminalSession&) = delete;

  [[nodiscard]] bool start(const std::filesystem::path& working_directory, int rows, int columns,
                           std::string& error);
  void stop();
  void resize(int rows, int columns);
  void poll();
  void send_text(const std::string& text);
  void send_key(TerminalKey key);

  [[nodiscard]] bool running() const noexcept;
  [[nodiscard]] const std::string& error() const noexcept;
  [[nodiscard]] std::vector<std::string> screen_lines() const;
  [[nodiscard]] std::vector<TerminalScreenLine> screen() const;
  [[nodiscard]] std::optional<TerminalCursor> cursor() const;

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace horcrux
