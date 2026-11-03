add_library(umbra_ads STATIC ad_manager.cc vast_parser.cc)
add_library(umbra::ads ALIAS umbra_ads)
set_target_properties(umbra_ads PROPERTIES EXPORT_NAME "umbra::ads")
target_include_directories(umbra_ads PUBLIC
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
  "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")
target_include_directories(umbra_ads PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/internal_include")
target_link_libraries(umbra_ads
  PUBLIC umbra::base penumbra::interface
  PRIVATE umbra::manifest absl::status absl::statusor)
alumbra_target_options(umbra_ads)
target_compile_options(umbra_ads PRIVATE -fno-exceptions)
if(ALUMBRA_BUILD_ADS)
  install(TARGETS umbra_ads EXPORT AlumbraTargets
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
  install(DIRECTORY include/ DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
endif()
