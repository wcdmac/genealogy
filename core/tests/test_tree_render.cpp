// 树渲染与 TreeService 测试（M6 补漏：防「TreeNode 悬空」回归）。
//
// 背景：TreeService 第一版把 PersonGraph 作局部变量再返回 TreeNode，
// 调用方拿到悬空指针——CLI io export tree 一跑就 SIGSEGV。
// 这类 bug 编译期零提示、运行时随机崩，唯一的防线是端到端测试
// 真的走一遍 export_tree（装配 + 渲染 + 文本断言）。

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>
#include <system_error>

#include <genealogy/io/tree_render.hpp>
#include <genealogy/lineage/graph.hpp>
#include <genealogy/lineage/tree.hpp>
#include <genealogy/repository/bootstrap.hpp>
#include <genealogy/repository/database.hpp>
#include <genealogy/service/person_service.hpp>
#include <genealogy/service/tree_service.hpp>
#include <genealogy/ulid.hpp>

using namespace genealogy;

namespace {

std::filesystem::path temp_db_path() {
  return std::filesystem::temp_directory_path() /
         ("genealogy-tree-render-" + generate_ulid() + ".db");
}

Person make_person(const std::string& family, const std::string& given,
                   const std::string& father) {
  Person p;
  p.id = generate_ulid();
  p.lineage_id = std::string(26, '0');
  p.family_name = family;
  p.given_name = given;
  p.gender = Gender::Male;
  if (!father.empty()) p.father_id = father;
  return p;
}

}  // namespace

TEST_CASE("树渲染：DOT 用 ID 作节点、姓名作标签", "[io][tree]") {
  const std::string root_id = generate_ulid();
  std::vector<Person> persons;
  persons.push_back(make_person("陈", "大", ""));  // 占位，id 另设
  persons.back().id = root_id;
  const std::string child_id = generate_ulid();
  auto child = make_person("陈", "二", root_id);
  child.id = child_id;
  persons.push_back(child);

  const lineage::PersonGraph graph =
      lineage::PersonGraph::build(persons, {});
  auto tree = lineage::descendants_tree(graph, root_id, 3);
  REQUIRE(tree.has_value());
  REQUIRE(tree->children.size() == 1);

  const std::string dot = io::render_tree(*tree, io::TreeFormat::Dot);
  CHECK(dot.find("digraph genealogy") != std::string::npos);
  CHECK(dot.find("rankdir=TB") != std::string::npos);
  // 节点标识是 ID，标签才是姓名——用姓名当 key 会把同名的两个人并点
  CHECK(dot.find("\"" + root_id + "\" [label=\"陈大\"]") != std::string::npos);
  CHECK(dot.find("\"" + child_id + "\" [label=\"陈二\"]") != std::string::npos);
  // 边方向：根 → 子
  CHECK(dot.find("\"" + root_id + "\" -> \"" + child_id + "\"") !=
        std::string::npos);
}

TEST_CASE("树渲染：Mermaid 语法与 DOT 同构", "[io][tree]") {
  const std::string root_id = generate_ulid();
  std::vector<Person> persons;
  auto root = make_person("陈", "大", "");
  root.id = root_id;
  persons.push_back(root);

  const lineage::PersonGraph graph =
      lineage::PersonGraph::build(persons, {});
  auto tree = lineage::descendants_tree(graph, root_id, 3);
  REQUIRE(tree.has_value());

  const std::string mermaid = io::render_tree(*tree, io::TreeFormat::Mermaid);
  CHECK(mermaid.find("graph TD") != std::string::npos);
  CHECK(mermaid.find("\"" + root_id + "\"[\"陈大\"]") != std::string::npos);
  // 单人树没有边
  CHECK(mermaid.find("-->") == std::string::npos);
}

TEST_CASE("TreeService：export_tree 端到端（防悬空回归）", "[service][tree]") {
  const auto path = temp_db_path();
  auto opened = Database::open(path);
  REQUIRE(opened);
  ensure_default_lineage(*opened);

  PersonService persons(*opened);
  Person draft;
  draft.lineage_id = std::string(26, '0');
  draft.family_name = "陈";
  draft.given_name = "始祖";
  draft.gender = Gender::Male;
  auto made_root = persons.create_person(draft);
  REQUIRE(made_root.has_value());

  Person child_draft;
  child_draft.lineage_id = std::string(26, '0');
  child_draft.family_name = "陈";
  child_draft.given_name = "二世";
  child_draft.gender = Gender::Male;
  child_draft.father_id = made_root->id;
  auto made_child = persons.create_person(child_draft);
  REQUIRE(made_child.has_value());

  TreeService trees(*opened);
  auto dot = trees.export_tree(made_root->id, io::TreeFormat::Dot, 5);
  REQUIRE(dot.has_value());
  // 端到端能走到这里且内容含子节点，即证明渲染时 graph 仍存活
  CHECK(dot->find(made_child->id) != std::string::npos);
  CHECK(dot->find("陈二世") != std::string::npos);

  auto mermaid = trees.export_tree(made_root->id, io::TreeFormat::Mermaid, 5);
  REQUIRE(mermaid.has_value());
  CHECK(mermaid->find("graph TD") != std::string::npos);

  // 不存在的人物 → 错误而非崩溃
  auto missing = trees.export_tree("NOSUCHPERSON000000000000",
                                   io::TreeFormat::Dot, 5);
  CHECK(!missing.has_value());

  std::error_code ec;
  std::filesystem::remove(path, ec);
}
