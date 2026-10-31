cmake_minimum_required(VERSION 3.28)
project(AlumbraLayeringPortLinksPort LANGUAGES NONE)
include("${CMAKE_CURRENT_LIST_DIR}/../common.cmake")

layering_fixture_library(penumbra_web penumbra::web)
target_link_libraries(penumbra_web INTERFACE penumbra::interface)
target_link_libraries(penumbra_desktop INTERFACE penumbra::web)

alumbra_write_link_graph("${CMAKE_BINARY_DIR}/link-graph.tsv")
