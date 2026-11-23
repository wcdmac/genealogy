// 内置文件浏览器的数据层。

#include <genealogy/gui/file_browser.hpp>

#include <algorithm>
#include <cctype>

namespace genealogy::gui {

namespace {
std::string lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return s;
}
}  // namespace

bool FileBrowser::cd(const std::string& dir, std::string* why) {
  std::error_code ec;
  const auto target = std::filesystem::absolute(dir, ec);
  if (ec) { if (why) *why = "路径无效: " + ec.message(); return false; }
  if (!std::filesystem::is_directory(target, ec)) {
    if (why) *why = "不是目录: " + dir;
    return false;
  }
  current_ = target;
  return true;
}

bool FileBrowser::refresh(std::string* why, const std::string& filter_ext) {
  entries_.clear();
  std::error_code ec;
  for (const auto& e : std::filesystem::directory_iterator(current_, ec)) {
    const auto name = e.path().filename().string();
    const bool is_dir = e.is_directory(ec);
    if (!is_dir && !filter_ext.empty() &&
        lower(e.path().extension().string()) != lower(filter_ext))
      continue;
    FileEntry fe;
    fe.name = name;
    fe.path = e.path();
    fe.is_dir = is_dir;
    fe.size = is_dir ? 0 : e.file_size(ec);
    entries_.push_back(std::move(fe));
  }
  if (ec) { if (why) *why = "读目录失败: " + ec.message(); return false; }
  std::stable_sort(entries_.begin(), entries_.end(),
                   [](const FileEntry& a, const FileEntry& b) {
                     if (a.is_dir != b.is_dir) return a.is_dir;
                     return a.name < b.name;
                   });
  return true;
}

std::string FileBrowser::resolve(const std::string& input) const {
  if (input.empty()) return {};
  std::error_code ec;
  auto p = std::filesystem::path(input);
  if (p.is_relative()) p = current_ / p;
  return std::filesystem::absolute(p, ec).lexically_normal().string();
}

}  // namespace genealogy::gui
