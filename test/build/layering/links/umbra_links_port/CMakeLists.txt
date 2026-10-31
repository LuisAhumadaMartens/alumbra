cmake_minimum_required(VERSION 3.28)
project(AlumbraLayeringUmbraLinksPort LANGUAGES CXX)
include("${CMAKE_CURRENT_LIST_DIR}/../common.cmake")

add_library(umbra_engine STATIC engine.cc)
add_library(umbra::engine ALIAS umbra_engine)
set_target_properties(umbra_engine PROPERTIES EXPORT_NAME "umbra::engine")
target_link_libraries(umbra_engine
  PUBLIC penumbra::interface
  PRIVATE penumbra::desktop)

alumbra_write_link_graph("${CMAKE_BINARY_DIR}/link-graph.tsv")
