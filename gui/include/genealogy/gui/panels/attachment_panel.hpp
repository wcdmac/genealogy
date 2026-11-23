#pragma once

// 照片/附件面板：文件导入（sha256 去重）、列表、按人物筛选。

#include <genealogy/gui/app_state.hpp>
#include <genealogy/gui/panels.hpp>

#include <genealogy/model/attachment.hpp>

#include <string>
#include <vector>

namespace genealogy::gui {

class AttachmentPanel : public Panel {
 public:
  explicit AttachmentPanel(AppState& state) : state_(state) {}

  const char* title() const override { return "照片附件"; }
  void draw(DbSession& session) override;
  void on_database_closed() override { clear(); }
  bool has_cached_data() const override { return !rows_.empty(); }

  bool reload(DbSession& session, std::string* why);
  bool import_file(DbSession& session, const std::string& path,
                   AttachmentKind kind, const std::string& subject_id,
                   const std::string& caption, std::string* why);
  bool remove(DbSession& session, const std::string& id, std::string* why);
  void clear();

  const std::vector<Attachment>& rows() const { return rows_; }
  void set_subject_filter(const std::string& id) { subject_buf_ = id; }

 private:
  AppState& state_;
  std::vector<Attachment> rows_;
  std::string path_buf_;
  std::string subject_buf_;
  std::string caption_buf_;
  int kind_idx_ = 2;  // photo
};

}  // namespace genealogy::gui
