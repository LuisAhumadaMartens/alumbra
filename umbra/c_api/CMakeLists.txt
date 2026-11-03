add_library(umbra_c_api STATIC c_api.cc)
add_library(umbra::c_api ALIAS umbra_c_api)
set_target_properties(umbra_c_api PROPERTIES EXPORT_NAME "umbra::c_api")
target_include_directories(umbra_c_api PUBLIC
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
  "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")
alumbra_target_options(umbra_c_api)
install(TARGETS umbra_c_api EXPORT AlumbraTargets
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
install(DIRECTORY include/ DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
