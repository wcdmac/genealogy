add_executable(genealogy-core-tests
  test_ulid.cpp
  test_genealogy_date.cpp
  test_person_repository.cpp
  test_repositories_m1.cpp
  test_services_m1.cpp
  test_lineage.cpp
)

target_link_libraries(genealogy-core-tests
  PRIVATE
    genealogy-core
    Catch2::Catch2WithMain
)

target_compile_features(genealogy-core-tests PRIVATE cxx_std_20)

add_test(NAME genealogy-core-tests COMMAND genealogy-core-tests)
