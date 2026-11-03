add_library(umbra_ads STATIC obstruction_registry.cc)
add_library(umbra::ads ALIAS umbra_ads)
set_target_properties(umbra_ads PROPERTIES EXPORT_NAME "umbra::ads")
target_include_directories(umbra_ads PUBLIC
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/internal_include>")
target_link_libraries(umbra_ads PUBLIC "$<BUILD_INTERFACE:absl::status>")
alumbra_target_options(umbra_ads)
target_compile_options(umbra_ads PRIVATE -fno-exceptions)
install(TARGETS umbra_ads EXPORT AlumbraTargets
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
