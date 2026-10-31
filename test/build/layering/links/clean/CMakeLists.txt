cmake_minimum_required(VERSION 3.28)
project(AlumbraLayeringClean LANGUAGES NONE)
include("${CMAKE_CURRENT_LIST_DIR}/../common.cmake")

add_library(Threads::Threads INTERFACE IMPORTED)
set_target_properties(Threads::Threads PROPERTIES
  INTERFACE_LINK_LIBRARIES "-pthread")
add_library(absl::time INTERFACE IMPORTED)
set_target_properties(absl::time PROPERTIES
  INTERFACE_LINK_LIBRARIES "-framework CoreFoundation")
add_library(SDL3::SDL3 INTERFACE IMPORTED)

layering_fixture_library(umbra_helper umbra::helper)
target_link_libraries(umbra_helper INTERFACE
  penumbra::interface absl::time Threads::Threads)
layering_fixture_library(umbra_engine umbra::engine)
target_link_libraries(umbra_engine INTERFACE umbra::helper)
target_link_libraries(penumbra_desktop INTERFACE
  SDL3::SDL3 "-framework CoreFoundation")

alumbra_write_link_graph("${CMAKE_BINARY_DIR}/link-graph.tsv")
