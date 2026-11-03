add_executable(alumbra_c_api_consumer c_api_consumer.c c_api_test.cc)
set_target_properties(alumbra_c_api_consumer PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)
alumbra_target_options(alumbra_c_api_consumer)
target_compile_options(alumbra_c_api_consumer PRIVATE
  "$<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions>")
target_link_libraries(alumbra_c_api_consumer PRIVATE
  umbra::c_api umbra::observability alumbra_test::fake_port GTest::gtest_main)
if(ALUMBRA_PORT STREQUAL "desktop")
  target_link_libraries(alumbra_c_api_consumer PRIVATE penumbra::desktop_port)
endif()
target_include_directories(alumbra_c_api_consumer PRIVATE
  "${PROJECT_SOURCE_DIR}/umbra/c_api/internal_include"
  "${PROJECT_SOURCE_DIR}/umbra/player/include"
  "${PROJECT_SOURCE_DIR}/umbra/player/internal_include"
  "${PROJECT_SOURCE_DIR}/umbra/pipeline/internal_include"
  "${PROJECT_SOURCE_DIR}/test/penumbra_interface"
  "${PROJECT_SOURCE_DIR}/test/umbra/pipeline")
alumbra_target_options(alumbra_c_api_consumer)
gtest_discover_tests(alumbra_c_api_consumer
  TEST_PREFIX "alumbra_c_api_consumer."
  PROPERTIES LABELS integration DISCOVERY_TIMEOUT 60)
