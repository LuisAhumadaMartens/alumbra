find_package(Python3 COMPONENTS Interpreter REQUIRED)
include("${PROJECT_SOURCE_DIR}/cmake/AlumbraLayering.cmake")
cmake_language(DEFER DIRECTORY "${PROJECT_SOURCE_DIR}"
  CALL alumbra_write_link_graph
  "${PROJECT_BINARY_DIR}/layering/link-graph-$<CONFIG>.tsv")

set(_layering_check "${PROJECT_SOURCE_DIR}/tools/layering/check_layering.py")
add_test(NAME layering_include_test
  COMMAND "${Python3_EXECUTABLE}" "${_layering_check}"
    includes --root "${PROJECT_SOURCE_DIR}")
add_test(NAME layering_target_graph_test
  COMMAND "${Python3_EXECUTABLE}" "${_layering_check}"
    links --graph "${PROJECT_BINARY_DIR}/layering/link-graph-$<CONFIG>.tsv")
set_tests_properties(layering_include_test layering_target_graph_test
  PROPERTIES LABELS layering)

add_executable(umbra_wasm_smoke_test
  ../umbra_base_smoke_test.cc
  ../umbra/base/bit_reader_test.cc
  ../umbra/base/byte_reader_test.cc)
target_link_libraries(umbra_wasm_smoke_test PRIVATE
  umbra::base GTest::gtest_main)
alumbra_target_options(umbra_wasm_smoke_test)
target_compile_options(umbra_wasm_smoke_test PRIVATE -fno-exceptions)
alumbra_add_test(TARGET umbra_wasm_smoke_test
  TEST_PREFIX "umbra_wasm_smoke_test."
  LABELS unit)
