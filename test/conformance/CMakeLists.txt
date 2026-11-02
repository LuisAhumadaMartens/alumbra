set(_conformance_tools "${PROJECT_SOURCE_DIR}/tools/conformance")
set(_assets "${_conformance_tools}/assets.py")
set(_assets_data "${CMAKE_CURRENT_BINARY_DIR}/conformance_assets.cmake")

set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/manifest.yaml"
  "${_assets}"
  "${_conformance_tools}/manifest.py"
  "${_conformance_tools}/yaml_subset.py"
  "${PROJECT_SOURCE_DIR}/settings/meta-schema.toml"
  "${PROJECT_SOURCE_DIR}/errors/catalogue.toml")
execute_process(
  COMMAND "${Python3_EXECUTABLE}" "${_assets}" cmake --output "${_assets_data}"
  RESULT_VARIABLE _assets_result
  ERROR_VARIABLE _assets_error)
if(NOT _assets_result EQUAL 0)
  message(FATAL_ERROR "The conformance manifest is invalid:\n${_assets_error}")
endif()
include("${_assets_data}")

foreach(_id IN LISTS ALUMBRA_CONFORMANCE_ASSET_IDS)
  set(_test "conformance.asset.${_id}")
  if(ALUMBRA_CONFORMANCE_ASSET_KIND_${_id} STREQUAL "remote")
    add_test(NAME "${_test}"
      COMMAND "${Python3_EXECUTABLE}" "${_assets}" probe --ids "${_id}")
    set_tests_properties("${_test}" PROPERTIES SKIP_RETURN_CODE 77)
  else()
    add_test(NAME "${_test}"
      COMMAND "${Python3_EXECUTABLE}" "${_assets}" fetch --ids "${_id}")
  endif()
  set_tests_properties("${_test}" PROPERTIES
    LABELS "conformance;${ALUMBRA_CONFORMANCE_ASSET_TAGS_${_id}}"
    FIXTURES_SETUP "${_test}")
  if(NOT ALUMBRA_CONFORMANCE_ASSETS)
    set_tests_properties("${_test}" PROPERTIES DISABLED TRUE)
  endif()
endforeach()

add_test(NAME conformance_manifest_validation
  COMMAND "${Python3_EXECUTABLE}" "${_assets}" validate)
add_test(NAME conformance_attribution_current
  COMMAND "${Python3_EXECUTABLE}" "${_assets}" attribution --check)
add_test(NAME conformance_tools_unittests
  COMMAND "${Python3_EXECUTABLE}" -m unittest discover
    -s "${_conformance_tools}" -p "test_*.py")
add_test(NAME conformance_tag_labels
  COMMAND "${Python3_EXECUTABLE}" "${_assets}" check-labels
    --ctest "${CMAKE_CTEST_COMMAND}"
    --build-dir "${PROJECT_BINARY_DIR}"
    --config "$<CONFIG>"
    --combination progressive,h264)
set_tests_properties(
  conformance_manifest_validation conformance_attribution_current
  conformance_tools_unittests conformance_tag_labels
  PROPERTIES LABELS conformance_tools)
