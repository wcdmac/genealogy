#pragma once

// 内置文件浏览器的数据层（无 ImGui 依赖，可无头测试）。
// 跨平台只用 std::filesystem；不调外部程序、不引入新依赖。

#include <filesystem>
#include <string>
#include <vector>

namespace genealogy::gui {

struct FileEntry {
  std::string name;
  std::filesystem::path path;
  bool is_dir = false;
  std::uintmax_t size = 0;
};

class FileBrowser {
 public:
  // 进入目录，失败（不存在/无权限）保留原目录并返回 false
  bool cd(const std::string& dir, std::string* why);

  // 刷新当前目录内容：目录在前、文件在后，各自按名字排序。
  // filter_ext 为空表示全部，否则仅列该扩展名（如 ".db"，大小写不敏感）。
  bool refresh(std::string* why, const std::string& filter_ext = ".db");

  const std::filesystem::path& current() const { return current_; }
  const std::vector<FileEntry>& entries() const { return entries_; }
  // 把当前输入路径规范化为绝对路径（相对路径按当前目录解析）
  std::string resolve(const std::string& input) const;

 private:
  std::filesystem::path current_;
  std::vector<FileEntry> entries_;
};

}  // namespace genealogy::gui
