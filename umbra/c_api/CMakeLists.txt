add_library(umbra_c_api STATIC c_api.cc)
add_library(umbra::c_api ALIAS umbra_c_api)
set_target_properties(umbra_c_api PROPERTIES EXPORT_NAME "umbra::c_api")
target_include_directories(umbra_c_api PUBLIC
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
  "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")
target_include_directories(umbra_c_api PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/internal_include"
  "${CMAKE_CURRENT_SOURCE_DIR}/../settings/internal_include")
if(CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
  set(_alumbra_settings_platform 8)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Android")
  set(_alumbra_settings_platform 3)
elseif(CMAKE_SYSTEM_NAME STREQUAL "iOS")
  set(_alumbra_settings_platform 5)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
  set(_alumbra_settings_platform 2)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
  set(_alumbra_settings_platform 1)
else()
  set(_alumbra_settings_platform 0)
endif()
target_compile_definitions(umbra_c_api PRIVATE
  ALUMBRA_SETTINGS_PLATFORM=${_alumbra_settings_platform})
target_link_libraries(umbra_c_api PRIVATE umbra::player umbra::settings)
alumbra_target_options(umbra_c_api)
target_compile_options(umbra_c_api PRIVATE -fno-exceptions)
install(TARGETS umbra_c_api EXPORT AlumbraTargets
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
install(DIRECTORY include/ DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
