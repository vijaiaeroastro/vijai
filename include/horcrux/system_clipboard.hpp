#pragma once

#include <optional>
#include <string>

namespace horcrux {

class SystemClipboard {
 public:
  [[nodiscard]] bool copy_text(const std::string& text, std::string& error) const;
  [[nodiscard]] std::optional<std::string> paste_text(std::string& error) const;
};

}  // namespace horcrux
