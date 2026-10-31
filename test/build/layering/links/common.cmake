include("${ALUMBRA_SOURCE_DIR}/cmake/AlumbraLayering.cmake")

function(layering_fixture_library target name)
  add_library(${target} INTERFACE)
  add_library(${name} ALIAS ${target})
  set_target_properties(${target} PROPERTIES EXPORT_NAME "${name}")
endfunction()

layering_fixture_library(penumbra_interface penumbra::interface)
layering_fixture_library(penumbra_desktop penumbra::desktop)
target_link_libraries(penumbra_desktop INTERFACE penumbra::interface)
