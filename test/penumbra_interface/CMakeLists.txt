find_package(absl CONFIG REQUIRED)

add_executable(penumbra_interface_test
  rational_test.cc
  media_time_test.cc
  shared_bytes_test.cc
  encryption_info_test.cc
  encoded_sample_test.cc
  stream_info_test.cc)
target_link_libraries(penumbra_interface_test PRIVATE
  penumbra::interface
  absl::int128
  absl::str_format
  absl::strings
  GTest::gtest_main)
alumbra_target_options(penumbra_interface_test)
gtest_discover_tests(penumbra_interface_test
  TEST_PREFIX "penumbra_interface_test.")

add_library(penumbra_fake_port STATIC fake_port.cc)
target_include_directories(penumbra_fake_port PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(penumbra_fake_port PUBLIC
  penumbra::interface umbra::base_test_support)
alumbra_target_options(penumbra_fake_port)

add_executable(penumbra_port_test port_test.cc)
target_link_libraries(penumbra_port_test PRIVATE
  penumbra_fake_port umbra::base_test_support GTest::gtest_main)
alumbra_target_options(penumbra_port_test)
gtest_discover_tests(penumbra_port_test TEST_PREFIX "penumbra_port_test.")

file(GLOB _internal_headers CONFIGURE_DEPENDS
  "${PROJECT_SOURCE_DIR}/penumbra/interface/internal_include/penumbra/interface/*.h")
set(_header_sources "")
foreach(_header IN LISTS _internal_headers)
  get_filename_component(_name "${_header}" NAME)
  set(_source "${CMAKE_CURRENT_BINARY_DIR}/headers/${_name}.cc")
  file(GENERATE OUTPUT "${_source}" CONTENT
    "#include \"penumbra/interface/${_name}\"\nstatic_assert(__cplusplus == 202002L);\n")
  list(APPEND _header_sources "${_source}")
endforeach()
add_library(penumbra_port_header_compile OBJECT ${_header_sources})
target_link_libraries(penumbra_port_header_compile PRIVATE penumbra::interface)
alumbra_target_options(penumbra_port_header_compile)
set_target_properties(penumbra_port_header_compile PROPERTIES
  CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
