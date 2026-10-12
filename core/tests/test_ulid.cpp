#include <catch2/catch_test_macros.hpp>

#include <set>
#include <string>

#include <genealogy/ulid.hpp>

using namespace genealogy;

TEST_CASE("ULID：26 字符 Crockford Base32 格式", "[ulid]") {
  const auto id = generate_ulid();
  REQUIRE(id.size() == 26);
  REQUIRE(is_valid_ulid(id));

  REQUIRE_FALSE(is_valid_ulid(""));
  REQUIRE_FALSE(is_valid_ulid(std::string(25, '0')));
  REQUIRE_FALSE(is_valid_ulid(std::string(27, '0')));
  REQUIRE_FALSE(is_valid_ulid(std::string(25, 'A') + 'I'));   // I 不在字母表
  REQUIRE_FALSE(is_valid_ulid(std::string(25, 'A') + 'a'));   // 小写不合法
  REQUIRE_FALSE(is_valid_ulid("01J8XAAAAAAAAAAAAAAAAAA-"));   // 连字符不合法
}

TEST_CASE("ULID：批量生成唯一", "[ulid]") {
  std::set<std::string> ids;
  for (int i = 0; i < 1000; ++i) {
    ids.insert(generate_ulid());
  }
  REQUIRE(ids.size() == 1000);
}
