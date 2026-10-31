add_library(umbra_ui STATIC ui.cc)
add_library(umbra::ui ALIAS umbra_ui)
set_target_properties(umbra_ui PROPERTIES EXPORT_NAME "umbra::ui")
target_link_libraries(umbra_ui PUBLIC penumbra::interface)
alumbra_target_options(umbra_ui)
install(TARGETS umbra_ui EXPORT AlumbraTargets
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
