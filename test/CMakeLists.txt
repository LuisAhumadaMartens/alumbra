add_executable(umbra_base_smoke_test umbra_base_smoke_test.cc)
target_link_libraries(umbra_base_smoke_test PRIVATE umbra::base)
alumbra_target_options(umbra_base_smoke_test)
add_test(NAME umbra_base_smoke_test COMMAND umbra_base_smoke_test)

add_test(NAME alumbra_installed_consumer
  COMMAND "${CMAKE_COMMAND}"
    "-DALUMBRA_BINARY_DIR=${PROJECT_BINARY_DIR}"
    "-DALUMBRA_CONSUMER_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/consumer"
    "-DALUMBRA_TEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/installed-consumer"
    "-DALUMBRA_CONFIG=$<CONFIG>"
    "-DALUMBRA_GENERATOR=${CMAKE_GENERATOR}"
    "-DALUMBRA_CXX_COMPILER=${CMAKE_CXX_COMPILER}"
    "-DALUMBRA_BUILD_UI=${ALUMBRA_BUILD_UI}"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/installed_consumer.cmake")
