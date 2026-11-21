add_executable(genealogy-core-tests
  test_ulid.cpp
  test_genealogy_date.cpp
  test_person_repository.cpp
  # test_repositories_m1.cpp   # TODO: 单谱单库测试重写（2026-11-21）
  # test_services_m1.cpp       # TODO: 同上
  test_lineage.cpp
  test_calendar.cpp
  test_era.cpp
  # test_gedcom.cpp             # TODO: 同上
  # test_io_integration.cpp     # TODO: 同上
  test_validation.cpp
  # test_generation_poem.cpp    # TODO: 同上
  test_rules_snapshot.cpp
  test_migration.cpp
  test_tree_render.cpp
  test_documents.cpp
  test_person_name.cpp
  test_provenance_migration.cpp
  test_writer_serialization.cpp
)

target_link_libraries(genealogy-core-tests
  PRIVATE
    genealogy-core
    Catch2::Catch2WithMain
)

target_compile_features(genealogy-core-tests PRIVATE cxx_std_20)

add_test(NAME genealogy-core-tests COMMAND genealogy-core-tests)
