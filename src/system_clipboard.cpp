#include "horcrux/system_clipboard.hpp"

#include <clip.h>

namespace horcrux {

bool SystemClipboard::copy_text(const std::string& text, std::string& error) const {
  error.clear();
  if (clip::set_text(text)) return true;
  error = "could not write to the system clipboard";
  return false;
}

std::optional<std::string> SystemClipboard::paste_text(std::string& error) const {
  error.clear();
  std::string text;
  if (clip::get_text(text)) return text;
  error = "could not read text from the system clipboard";
  return std::nullopt;
}

}  // namespace horcrux
