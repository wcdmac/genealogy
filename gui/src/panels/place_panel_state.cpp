// 地点面板状态机（无 ImGui 依赖）。

#include <genealogy/gui/panels/place_panel.hpp>

#include <genealogy/ulid.hpp>

#include <cstdlib>
#include <cmath>

namespace genealogy::gui {

void PlacePanel::sync_draft() {
  draft_.name = name_buf_;
  draft_.modern_admin = modern_buf_.empty()
      ? std::nullopt : std::optional<std::string>(modern_buf_);
  draft_.historical_name = hist_buf_.empty()
      ? std::nullopt : std::optional<std::string>(hist_buf_);
  draft_.note = note_buf_.empty() ? std::nullopt : std::optional<std::string>(note_buf_);
}

bool PlacePanel::reload(DbSession& session, std::string* why) {
  PlaceFilter f;
  f.limit = 200;
  auto list = session.places().list_places(f);
  if (!list) { *why = list.error().message(); return false; }
  rows_ = *list;
  return true;
}

bool PlacePanel::save(DbSession& session, std::string* why) {
  if (draft_.name.empty()) { *why = "地点名不能为空"; return false; }
  // 经纬度：空串表示未设置；非法数值拒绝，不静默丢
  if (!lat_buf_.empty()) {
    char* end = nullptr;
    const double v = std::strtod(lat_buf_.c_str(), &end);
    if (end == lat_buf_.c_str() || *end != '\0' || !std::isfinite(v) ||
        v < -90 || v > 90) {
      *why = "纬度须在 -90..90";
      return false;
    }
    draft_.lat = v;
  } else draft_.lat = std::nullopt;
  if (!lng_buf_.empty()) {
    char* end = nullptr;
    const double v = std::strtod(lng_buf_.c_str(), &end);
    if (end == lng_buf_.c_str() || *end != '\0' || !std::isfinite(v) ||
        v < -180 || v > 180) {
      *why = "经度须在 -180..180";
      return false;
    }
    draft_.lng = v;
  } else draft_.lng = std::nullopt;

  const bool is_new = draft_.id.empty();
  if (is_new) draft_.id = generate_ulid();
  Result<Place> saved = is_new ? session.places().create_place(draft_)
                               : session.places().update_place(draft_);
  if (!saved) { *why = saved.error().message(); return false; }
  draft_ = *saved;
  reload(session, why);
  return true;
}

void PlacePanel::select(const Place& p) {
  draft_ = p;
  name_buf_ = p.name;
  modern_buf_ = p.modern_admin.value_or("");
  hist_buf_ = p.historical_name.value_or("");
  lat_buf_ = p.lat ? std::to_string(*p.lat) : "";
  lng_buf_ = p.lng ? std::to_string(*p.lng) : "";
  note_buf_ = p.note.value_or("");
}

void PlacePanel::new_draft() {
  draft_ = Place{};
  name_buf_.clear(); modern_buf_.clear(); hist_buf_.clear();
  lat_buf_.clear(); lng_buf_.clear(); note_buf_.clear();
}

void PlacePanel::clear() {
  loaded_db_.clear();
  loaded_ = false;
  rows_.clear();
  new_draft();
}

}  // namespace genealogy::gui
