cmake_minimum_required(VERSION 3.28)
project(AlumbraLayeringUmbraReachesPlatformLibrary LANGUAGES NONE)
include("${CMAKE_CURRENT_LIST_DIR}/../common.cmake")

add_library(SDL3::SDL3 INTERFACE IMPORTED)
layering_fixture_library(umbra_helper umbra::helper)
target_link_libraries(umbra_helper INTERFACE SDL3::SDL3)
layering_fixture_library(umbra_engine umbra::engine)
target_link_libraries(umbra_engine INTERFACE
  penumbra::interface umbra::helper)

alumbra_write_link_graph("${CMAKE_BINARY_DIR}/link-graph.tsv")
