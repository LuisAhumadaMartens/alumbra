add_library(penumbra_desktop STATIC desktop.cc)
add_library(penumbra::desktop ALIAS penumbra_desktop)
set_target_properties(penumbra_desktop PROPERTIES EXPORT_NAME "penumbra::desktop")
target_link_libraries(penumbra_desktop PUBLIC penumbra::interface)
alumbra_target_options(penumbra_desktop)
install(TARGETS penumbra_desktop EXPORT AlumbraTargets
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
