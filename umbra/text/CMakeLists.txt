add_library(umbra_text STATIC caption_layout.cc layout.cc ttml_parser.cc)
add_library(umbra::text ALIAS umbra_text)
target_include_directories(umbra_text PUBLIC
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/internal_include>")
target_link_libraries(umbra_text PUBLIC
  umbra::player
  PRIVATE
  umbra::base
  SheenBidi::SheenBidi
  libunibreak::libunibreak)
alumbra_target_options(umbra_text)
target_compile_options(umbra_text PRIVATE -fno-exceptions)
