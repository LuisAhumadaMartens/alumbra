add_library(umbra_base STATIC base.cc)
add_library(umbra::base ALIAS umbra_base)
set_target_properties(umbra_base PROPERTIES EXPORT_NAME "umbra::base")
target_include_directories(umbra_base PUBLIC
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
  "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")
target_link_libraries(umbra_base PUBLIC penumbra::interface)
alumbra_target_options(umbra_base)
install(TARGETS umbra_base EXPORT AlumbraTargets
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
install(DIRECTORY include/ DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
