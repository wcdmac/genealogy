add_executable(genealogy-core-tests
  test_ulid.cpp
  test_book_html.cpp
  test_chart_svg.cpp
  test_markup.cpp
  test_layout_svg.cpp
  test_page_layout.cpp
  test_genealogy_date.cpp
  test_person_repository.cpp
  test_repositories_m1.cpp
  test_services_m1.cpp
  test_lineage.cpp
  test_calendar.cpp
  test_era.cpp
  test_gedcom.cpp
  test_io_integration.cpp
  test_validation.cpp
  test_generation_poem.cpp
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
