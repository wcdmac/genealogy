#pragma once

// GUI 的数据库会话：持有连接与用例层服务实例。
//
// 为什么单独一层：ADR-1 要求 GUI 只经用例层访问数据（不得直连仓储）。
// 面板全部通过 DbSession 拿服务，同一份会话逻辑在 GUI 与无头测试里
// 跑的是同一条代码路径——「测试过、线上挂」的风险因此小很多。
//
// 生命周期：一次只开一个库（桌面单机形态）。换库 = close + open。

#include <genealogy/repository/database.hpp>
#include <genealogy/service/branch_service.hpp>
#include <genealogy/service/event_service.hpp>
#include <genealogy/service/attachment_service.hpp>
#include <genealogy/service/generation_poem_service.hpp>
#include <genealogy/repository/book_repository.hpp>
#include <genealogy/service/person_service.hpp>
#include <genealogy/service/place_service.hpp>
#include <genealogy/service/union_service.hpp>

#include <memory>
#include <string>

namespace genealogy::gui {

class DbSession {
 public:
  DbSession() = default;
  ~DbSession();
  DbSession(const DbSession&) = delete;
  DbSession& operator=(const DbSession&) = delete;

  // 打开（或新建）数据库文件。失败时 why 带原因，返回 false。
  bool open(const std::string& path, std::string* why);

  // 关闭当前库。幂等。
  void close();

  bool is_open() const noexcept { return db_ != nullptr; }
  const std::string& path() const noexcept { return path_; }

  Database& db() noexcept { return *db_; }
  PersonService& persons() noexcept { return *persons_; }
  UnionService& unions() noexcept { return *unions_; }
  EventService& events() noexcept { return *events_; }
  BookRepository& books() noexcept { return *books_; }
  BranchService& branches() noexcept { return *branches_; }
  PlaceService& places() noexcept { return *places_; }
  GenerationPoemService& poems() noexcept { return *poems_; }
  AttachmentService& attachments() noexcept { return *attachments_; }

 private:
  std::string path_;
  std::unique_ptr<Database> db_;
  // 服务对象在数据库打开后才构造（构造要 Database&）。
  // 用 optional 之外的惰性指针太绕，这里用 unique_ptr + 重建。
  std::unique_ptr<PersonService> persons_;
  std::unique_ptr<UnionService> unions_;
  std::unique_ptr<EventService> events_;
  std::unique_ptr<BookRepository> books_;
  std::unique_ptr<BranchService> branches_;
  std::unique_ptr<PlaceService> places_;
  std::unique_ptr<GenerationPoemService> poems_;
  std::unique_ptr<AttachmentService> attachments_;

  void rebuild_services();
};

}  // namespace genealogy::gui
