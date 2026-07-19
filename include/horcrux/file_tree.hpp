#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace horcrux {

struct FileTreeEntry {
  std::filesystem::path relative_path;
  std::size_t depth{0};
  bool directory{false};
  bool expanded{false};
};

class FileTree {
 public:
  explicit FileTree(std::filesystem::path root, std::size_t maximum_entries = 10000U);

  bool refresh(std::string& error);
  [[nodiscard]] const std::filesystem::path& root() const noexcept;
  [[nodiscard]] const std::vector<FileTreeEntry>& entries() const noexcept;
  [[nodiscard]] std::size_t selected_index() const noexcept;
  [[nodiscard]] const FileTreeEntry* selected_entry() const noexcept;

  void select(std::size_t index) noexcept;
  void select_previous() noexcept;
  void select_next() noexcept;
  bool toggle_selected(std::string& error);
  bool toggle_hidden_files(std::string& error);
  [[nodiscard]] bool showing_hidden_files() const noexcept;

 private:
  bool append_directory(const std::filesystem::path& relative_directory,
                        std::size_t depth, std::string& error);
  [[nodiscard]] bool should_ignore(const std::filesystem::path& relative_path) const;

  std::filesystem::path root_;
  std::size_t maximum_entries_;
  std::vector<FileTreeEntry> entries_;
  std::set<std::filesystem::path> expanded_directories_;
  std::size_t selected_index_{0};
  bool showing_hidden_files_{false};
};

}  // namespace horcrux
