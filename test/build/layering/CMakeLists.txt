find_package(Python3 COMPONENTS Interpreter)
if(NOT Python3_Interpreter_FOUND)
  message(FATAL_ERROR
    "The layering tests need a Python 3 interpreter. Install python3 or "
    "configure with ALUMBRA_BUILD_TESTS=OFF.")
endif()

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

function(_alumbra_layering_fixture check fixture expect regex)
  if(check STREQUAL "includes")
    set(_test "layering_include_test.${fixture}")
  else()
    set(_test "layering_target_graph_test.${fixture}")
  endif()
  add_test(NAME "${_test}"
    COMMAND "${CMAKE_COMMAND}"
      "-DLAYERING_CHECK=${check}"
      "-DLAYERING_FIXTURE=${CMAKE_CURRENT_SOURCE_DIR}/${check}/${fixture}"
      "-DLAYERING_WORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/${check}/${fixture}"
      "-DLAYERING_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
      "-DLAYERING_PYTHON=${Python3_EXECUTABLE}"
      "-DLAYERING_GENERATOR=${CMAKE_GENERATOR}"
      "-DLAYERING_CACHE=${_consumer_cache_file}"
      "-DLAYERING_EXPECT=${expect}"
      "-DLAYERING_EXPECT_REGEX=${regex}"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/run_fixture.cmake")
  set_tests_properties("${_test}" PROPERTIES LABELS layering)
endfunction()

_alumbra_layering_fixture(includes clean pass "")
_alumbra_layering_fixture(includes umbra_platform_header fail
  "umbra/engine[.]cc:1: umbra may not include <pthread[.]h>")
_alumbra_layering_fixture(includes umbra_relative_escape fail
  "umbra/engine[.]cc:1: umbra may not include .[.][.]/penumbra/desktop/desktop[.]h. [(]resolves to penumbra/desktop/desktop[.]h[)]")
_alumbra_layering_fixture(includes interface_includes_umbra fail
  "penumbra/interface/include/penumbra/interface/interface[.]h:4: penumbra interface may not include .umbra/base/base[.]h.")
_alumbra_layering_fixture(includes port_includes_port fail
  "penumbra/desktop/desktop[.]cc:1: port desktop may not include .penumbra/web/web[.]h. from port web")

_alumbra_layering_fixture(links clean pass "")
_alumbra_layering_fixture(links umbra_links_port fail
  "umbra: edge umbra::engine -> penumbra::desktop is not allowed")
_alumbra_layering_fixture(links umbra_reaches_platform_library fail
  "umbra: edge umbra::helper -> SDL3::SDL3 is not allowed; path: umbra::engine -> umbra::helper -> SDL3::SDL3")
_alumbra_layering_fixture(links port_links_port fail
  "ports: edge penumbra::desktop -> penumbra::web links port penumbra::web from port penumbra::desktop")
