set(_alumbra_fuzz_root "${CMAKE_CURRENT_LIST_DIR}/..")
cmake_path(NORMAL_PATH _alumbra_fuzz_root)

function(alumbra_setup_fuzzing)
  add_custom_target(alumbra_fuzz)
  add_custom_target(alumbra_fuzz_canaries)
  if(NOT ALUMBRA_FUZZING)
    return()
  endif()
  if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    message(FATAL_ERROR
      "ALUMBRA_FUZZING requires upstream Clang with libFuzzer; found "
      "${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}. Apple Clang and "
      "GCC do not ship libFuzzer: use the LLVM release in tools/ci/llvm-major "
      "on Linux or Homebrew LLVM on macOS.")
  endif()
  foreach(_budget IN ITEMS ALUMBRA_FUZZ_RUNS ALUMBRA_FUZZ_SEED
      ALUMBRA_FUZZ_RSS_LIMIT_MB ALUMBRA_FUZZ_TIMEOUT)
    if(NOT "${${_budget}}" MATCHES "^[1-9][0-9]*$")
      message(FATAL_ERROR
        "${_budget} must be a positive integer; found '${${_budget}}'.")
    endif()
  endforeach()
  include(CheckCXXSourceCompiles)
  set(CMAKE_REQUIRED_FLAGS "-fsanitize=fuzzer")
  check_cxx_source_compiles([=[
#include <cstddef>
#include <cstdint>
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t*, std::size_t) {
  return 0;
}
]=] ALUMBRA_HAVE_LIBFUZZER)
  if(NOT ALUMBRA_HAVE_LIBFUZZER)
    message(FATAL_ERROR
      "${CMAKE_CXX_COMPILER} cannot link -fsanitize=fuzzer. Install its "
      "compiler-rt libFuzzer runtime (on Ubuntu, libclang-rt-<major>-dev) "
      "and reconfigure in a fresh build directory.")
  endif()
endfunction()

function(_alumbra_fuzz_harness target)
  cmake_parse_arguments(PARSE_ARGV 1 _arg "" "" "SOURCES;LIBRARIES")
  target_sources(${target} PRIVATE ${_arg_SOURCES})
  target_include_directories(${target} PRIVATE
    "${_alumbra_fuzz_root}/test/fuzz")
  target_link_libraries(${target} PRIVATE ${_arg_LIBRARIES})
  alumbra_target_options(${target})
endfunction()

function(_alumbra_add_libfuzzer_executable target)
  cmake_parse_arguments(PARSE_ARGV 1 _arg "" "" "SOURCES;LIBRARIES")
  add_executable(${target})
  _alumbra_fuzz_harness(${target}
    SOURCES ${_arg_SOURCES} LIBRARIES ${_arg_LIBRARIES})
  target_link_options(${target} PRIVATE -fsanitize=fuzzer)
endfunction()

function(_alumbra_add_fuzz_run_test)
  cmake_parse_arguments(PARSE_ARGV 0 _arg ""
    "TEST;TARGET;DICT;EXPECT;LABEL" "CORPUS")
  set(_command
    "${Python3_EXECUTABLE}" "${_alumbra_fuzz_root}/tools/ci/run_fuzz_target.py"
    --name "${_arg_TARGET}"
    --executable "$<TARGET_FILE:${_arg_TARGET}>"
    --work-dir "${CMAKE_BINARY_DIR}/fuzz"
    --runs "${ALUMBRA_FUZZ_RUNS}"
    --seed "${ALUMBRA_FUZZ_SEED}"
    --rss-limit-mb "${ALUMBRA_FUZZ_RSS_LIMIT_MB}"
    --timeout "${ALUMBRA_FUZZ_TIMEOUT}")
  foreach(_directory IN LISTS _arg_CORPUS)
    list(APPEND _command --corpus "${_directory}")
  endforeach()
  if(_arg_DICT)
    list(APPEND _command --dict "${_arg_DICT}")
  endif()
  if(_arg_EXPECT)
    list(APPEND _command --expect "${_arg_EXPECT}")
  endif()
  add_test(NAME ${_arg_TEST} COMMAND ${_command})
  set_tests_properties(${_arg_TEST} PROPERTIES LABELS ${_arg_LABEL})
endfunction()

function(alumbra_add_fuzz_target)
  cmake_parse_arguments(PARSE_ARGV 0 _fuzz "" "NAME;DICT"
    "SOURCES;CORPUS;LIBRARIES")
  if(_fuzz_UNPARSED_ARGUMENTS OR _fuzz_KEYWORDS_MISSING_VALUES)
    message(FATAL_ERROR
      "alumbra_add_fuzz_target: unexpected or empty arguments "
      "${_fuzz_UNPARSED_ARGUMENTS} ${_fuzz_KEYWORDS_MISSING_VALUES}")
  endif()
  if(NOT _fuzz_NAME MATCHES "^[a-z][a-z0-9_]*_fuzzer$")
    message(FATAL_ERROR
      "alumbra_add_fuzz_target: NAME '${_fuzz_NAME}' must be lower snake "
      "case ending in _fuzzer.")
  endif()
  if(NOT _fuzz_SOURCES OR NOT _fuzz_CORPUS)
    message(FATAL_ERROR
      "alumbra_add_fuzz_target(${_fuzz_NAME}) needs SOURCES and CORPUS.")
  endif()
  set(_corpus "")
  foreach(_directory IN LISTS _fuzz_CORPUS)
    cmake_path(ABSOLUTE_PATH _directory
      BASE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" NORMALIZE
      OUTPUT_VARIABLE _path)
    if(NOT IS_DIRECTORY "${_path}")
      message(FATAL_ERROR
        "alumbra_add_fuzz_target(${_fuzz_NAME}): CORPUS directory "
        "'${_path}' does not exist.")
    endif()
    list(APPEND _corpus "${_path}")
  endforeach()
  set(_dict "")
  if(DEFINED _fuzz_DICT)
    cmake_path(ABSOLUTE_PATH _fuzz_DICT
      BASE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" NORMALIZE
      OUTPUT_VARIABLE _dict)
    if(NOT EXISTS "${_dict}" OR IS_DIRECTORY "${_dict}")
      message(FATAL_ERROR
        "alumbra_add_fuzz_target(${_fuzz_NAME}): DICT '${_dict}' is not a "
        "file.")
    endif()
  endif()

  add_executable(${_fuzz_NAME}_replay
    "${_alumbra_fuzz_root}/test/fuzz/replay_main.cc")
  _alumbra_fuzz_harness(${_fuzz_NAME}_replay
    SOURCES ${_fuzz_SOURCES} LIBRARIES ${_fuzz_LIBRARIES})
  add_test(NAME fuzz_replay.${_fuzz_NAME}
    COMMAND ${_fuzz_NAME}_replay ${_corpus})
  set_tests_properties(fuzz_replay.${_fuzz_NAME}
    PROPERTIES LABELS fuzz_replay)
  add_dependencies(alumbra_fuzz ${_fuzz_NAME}_replay)

  if(ALUMBRA_FUZZING)
    _alumbra_add_libfuzzer_executable(${_fuzz_NAME}
      SOURCES ${_fuzz_SOURCES} LIBRARIES ${_fuzz_LIBRARIES})
    _alumbra_add_fuzz_run_test(TEST fuzz_smoke.${_fuzz_NAME}
      TARGET ${_fuzz_NAME} CORPUS ${_corpus} DICT "${_dict}"
      LABEL fuzz_smoke)
    add_dependencies(alumbra_fuzz ${_fuzz_NAME})
  endif()
endfunction()

function(alumbra_add_fuzz_canary kind)
  cmake_parse_arguments(PARSE_ARGV 1 _arg "" "" "SOURCES;CORPUS")
  set(_target fuzz_canary_${kind})
  _alumbra_add_libfuzzer_executable(${_target} SOURCES ${_arg_SOURCES})
  target_compile_definitions(${_target} PRIVATE
    "ALUMBRA_FUZZ_RSS_LIMIT_MB=${ALUMBRA_FUZZ_RSS_LIMIT_MB}")
  set(_corpus "")
  foreach(_directory IN LISTS _arg_CORPUS)
    cmake_path(ABSOLUTE_PATH _directory
      BASE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" NORMALIZE
      OUTPUT_VARIABLE _path)
    list(APPEND _corpus "${_path}")
  endforeach()
  _alumbra_add_fuzz_run_test(TEST fuzz_canary.${kind} TARGET ${_target}
    CORPUS ${_corpus} EXPECT ${kind} LABEL fuzz_canary)
  add_dependencies(alumbra_fuzz_canaries ${_target})
endfunction()
