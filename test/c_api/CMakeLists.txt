add_executable(alumbra_c_api_consumer c_api_consumer.c)
set_target_properties(alumbra_c_api_consumer PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)
alumbra_target_options(alumbra_c_api_consumer)
target_link_libraries(alumbra_c_api_consumer PRIVATE umbra::c_api)
add_test(NAME alumbra_c_api_consumer COMMAND alumbra_c_api_consumer)
