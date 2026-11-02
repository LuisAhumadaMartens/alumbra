include_guard(GLOBAL)

set(ALUMBRA_TEST_KINDS
  unit integration conformance golden benchmark fuzz sanitizer_canary)
set(ALUMBRA_TEST_MODIFIERS network slow quarantine)
set(ALUMBRA_TEST_CATEGORIES
  layering cxx_probe conformance_tools design_tokens
  fuzz_replay fuzz_smoke fuzz_canary api_conformance fake_port)

set(_ALUMBRA_LABEL_IMPLIES_KIND
  layering=integration
  cxx_probe=integration
  conformance_tools=integration
  design_tokens=golden
  fuzz_replay=fuzz
  fuzz_smoke=fuzz
  fuzz_canary=fuzz)
set(_ALUMBRA_LABEL_IMPLIES_MODIFIER
  remote=network)
set(_ALUMBRA_TARGET_KINDS
  umbra_api_conformance_tests=conformance
  penumbra_fake_port_conformance_test=conformance
  penumbra_reference_player_test=integration
  ffmpeg_decoder_test=integration)
set(_ALUMBRA_NAME_KINDS
  "^penumbra_broken_port\\.=conformance"
  "^penumbra_conformance_report$=conformance"
  "(_generator|\\.unit|_unit|_unittests)$=unit")

function(_alumbra_lookup table key out)
  set(${out} "" PARENT_SCOPE)
  foreach(_entry IN LISTS ${table})
    string(FIND "${_entry}" "=" _separator REVERSE)
    string(SUBSTRING "${_entry}" 0 ${_separator} _entry_key)
    math(EXPR _value_start "${_separator} + 1")
    string(SUBSTRING "${_entry}" ${_value_start} -1 _entry_value)
    if("${key}" STREQUAL "${_entry_key}")
      set(${out} "${_entry_value}" PARENT_SCOPE)
      return()
    endif()
  endforeach()
endfunction()

function(_alumbra_kinds_in labels out)
  set(_found "")
  foreach(_label IN LISTS labels)
    if(_label IN_LIST ALUMBRA_TEST_KINDS)
      list(APPEND _found "${_label}")
    endif()
  endforeach()
  set(${out} "${_found}" PARENT_SCOPE)
endfunction()

function(_alumbra_complete_labels labels default_kind out)
  set(_labels "${labels}")
  _alumbra_kinds_in("${_labels}" _kinds)
  if(NOT _kinds)
    foreach(_label IN LISTS _labels)
      _alumbra_lookup(_ALUMBRA_LABEL_IMPLIES_KIND "${_label}" _implied)
      if(_implied)
        set(_kinds "${_implied}")
        break()
      endif()
    endforeach()
  endif()
  if(NOT _kinds)
    set(_kinds "${default_kind}")
  endif()
  list(APPEND _labels ${_kinds})
  foreach(_label IN LISTS _labels)
    _alumbra_lookup(_ALUMBRA_LABEL_IMPLIES_MODIFIER "${_label}" _implied)
    if(_implied)
      list(APPEND _labels "${_implied}")
    endif()
  endforeach()
  list(REMOVE_DUPLICATES _labels)
  set(${out} "${_labels}" PARENT_SCOPE)
endfunction()

function(_alumbra_validate_labels context labels)
  _alumbra_kinds_in("${labels}" _kinds)
  list(LENGTH _kinds _count)
  if(NOT _count EQUAL 1)
    message(FATAL_ERROR
      "${context} needs exactly one kind label from ${ALUMBRA_TEST_KINDS}, "
      "got '${_kinds}'.")
  endif()
endfunction()

function(_alumbra_validate_known context labels)
  foreach(_label IN LISTS labels)
    if(NOT _label IN_LIST ALUMBRA_TEST_KINDS
        AND NOT _label IN_LIST ALUMBRA_TEST_MODIFIERS
        AND NOT _label IN_LIST ALUMBRA_TEST_CATEGORIES)
      message(FATAL_ERROR
        "${context} uses the unregistered label '${_label}'. Register it in "
        "cmake/AlumbraTesting.cmake and docs/testing.md first.")
    endif()
  endforeach()
endfunction()

function(_alumbra_quarantine_labels context issue labels out)
  if(issue)
    if(NOT issue MATCHES "^[1-9][0-9]*$")
      message(FATAL_ERROR
        "${context}: QUARANTINE needs a GitHub issue number, got '${issue}'.")
    endif()
    list(APPEND labels quarantine)
  elseif("quarantine" IN_LIST labels)
    message(FATAL_ERROR
      "${context}: the quarantine label needs QUARANTINE <issue-number>.")
  endif()
  set(${out} "${labels}" PARENT_SCOPE)
endfunction()

function(alumbra_add_test)
  cmake_parse_arguments(PARSE_ARGV 0 _test
    "" "NAME;TARGET;TEST_PREFIX;QUARANTINE;WORKING_DIRECTORY"
    "COMMAND;LABELS;PROPERTIES")
  if(_test_UNPARSED_ARGUMENTS OR _test_KEYWORDS_MISSING_VALUES)
    message(FATAL_ERROR
      "alumbra_add_test: unexpected or empty arguments "
      "${_test_UNPARSED_ARGUMENTS} ${_test_KEYWORDS_MISSING_VALUES}")
  endif()
  if(_test_NAME AND _test_COMMAND AND NOT _test_TARGET)
    set(_context "alumbra_add_test(${_test_NAME})")
  elseif(_test_TARGET AND NOT _test_NAME AND NOT _test_COMMAND)
    set(_context "alumbra_add_test(${_test_TARGET})")
  else()
    message(FATAL_ERROR
      "alumbra_add_test needs NAME with COMMAND, or TARGET, and LABELS.")
  endif()
  _alumbra_quarantine_labels("${_context}" "${_test_QUARANTINE}"
    "${_test_LABELS}" _labels)
  _alumbra_validate_labels("${_context}" "${_labels}")
  _alumbra_validate_known("${_context}" "${_labels}")
  set(_properties "")
  if(_test_QUARANTINE)
    list(APPEND _properties ALUMBRA_QUARANTINE_ISSUE "${_test_QUARANTINE}")
  endif()
  list(APPEND _properties ${_test_PROPERTIES})
  if(_test_TARGET)
    string(REPLACE ";" "\\;" _escaped_labels "${_labels}")
    set(_discover_arguments "${_test_TARGET}")
    if(_test_TEST_PREFIX)
      list(APPEND _discover_arguments TEST_PREFIX "${_test_TEST_PREFIX}")
    else()
      list(APPEND _discover_arguments TEST_PREFIX "${_test_TARGET}.")
    endif()
    if(_test_WORKING_DIRECTORY)
      list(APPEND _discover_arguments
        WORKING_DIRECTORY "${_test_WORKING_DIRECTORY}")
    endif()
    _gtest_discover_tests(${_discover_arguments}
      PROPERTIES LABELS "${_escaped_labels}" ${_properties})
  else()
    add_test(NAME "${_test_NAME}" COMMAND ${_test_COMMAND})
    set_tests_properties("${_test_NAME}"
      PROPERTIES LABELS "${_labels}" ${_properties})
  endif()
endfunction()

function(gtest_discover_tests target)
  set(_arguments "")
  set(_labels_seen FALSE)
  set(_properties_seen FALSE)
  set(_after_labels FALSE)
  set(_explicit_labels "")
  _alumbra_lookup(_ALUMBRA_TARGET_KINDS "${target}" _kind)
  if(NOT _kind)
    set(_kind unit)
  endif()
  if(ARGC GREATER 1)
    math(EXPR _last "${ARGC} - 1")
    set(_positions "")
    foreach(_i RANGE 1 ${_last})
      list(APPEND _positions ${_i})
    endforeach()
  else()
    set(_positions "")
  endif()
  foreach(_position IN LISTS _positions)
    set(_argument "${ARGV${_position}}")
    if(_after_labels)
      set(_argument_is_labels TRUE)
      foreach(_label IN LISTS _argument)
        if(NOT _label IN_LIST ALUMBRA_TEST_KINDS
            AND NOT _label IN_LIST ALUMBRA_TEST_MODIFIERS
            AND NOT _label IN_LIST ALUMBRA_TEST_CATEGORIES)
          set(_argument_is_labels FALSE)
        endif()
      endforeach()
      if(_argument_is_labels)
        list(APPEND _explicit_labels ${_argument})
      else()
        _alumbra_complete_labels("${_explicit_labels}" "${_kind}" _completed)
        string(REPLACE ";" "\\\\;" _completed "${_completed}")
        list(APPEND _arguments "${_completed}" "${_argument}")
        set(_explicit_labels "")
        set(_after_labels FALSE)
      endif()
    elseif(_properties_seen AND _argument STREQUAL "LABELS")
      list(APPEND _arguments LABELS)
      set(_labels_seen TRUE)
      set(_after_labels TRUE)
    elseif(_argument STREQUAL "PROPERTIES")
      list(APPEND _arguments PROPERTIES)
      set(_properties_seen TRUE)
    else()
      list(APPEND _arguments "${_argument}")
    endif()
  endforeach()
  if(_after_labels)
    _alumbra_complete_labels("${_explicit_labels}" "${_kind}" _completed)
    string(REPLACE ";" "\\\\;" _completed "${_completed}")
    list(APPEND _arguments "${_completed}")
  endif()
  if(NOT _labels_seen)
    _alumbra_complete_labels("" "${_kind}" _completed)
    if(_properties_seen)
      list(FIND _arguments PROPERTIES _index)
      math(EXPR _index "${_index} + 1")
      list(INSERT _arguments ${_index} LABELS "${_completed}")
    else()
      list(APPEND _arguments PROPERTIES LABELS "${_completed}")
    endif()
  endif()
  _gtest_discover_tests(${target} ${_arguments})
endfunction()

function(_alumbra_test_directories directory out)
  set(_directories "${directory}")
  get_property(_children DIRECTORY "${directory}" PROPERTY SUBDIRECTORIES)
  foreach(_child IN LISTS _children)
    _alumbra_test_directories("${_child}" _nested)
    list(APPEND _directories ${_nested})
  endforeach()
  set(${out} "${_directories}" PARENT_SCOPE)
endfunction()

function(_alumbra_label_registered_tests)
  _alumbra_test_directories("${PROJECT_SOURCE_DIR}" _directories)
  foreach(_directory IN LISTS _directories)
    get_property(_tests DIRECTORY "${_directory}" PROPERTY TESTS)
    foreach(_name IN LISTS _tests)
      get_property(_labels TEST "${_name}" DIRECTORY "${_directory}"
        PROPERTY LABELS)
      get_property(_issue TEST "${_name}" DIRECTORY "${_directory}"
        PROPERTY ALUMBRA_QUARANTINE_ISSUE)
      set(_kind integration)
      foreach(_rule IN LISTS _ALUMBRA_NAME_KINDS)
        string(FIND "${_rule}" "=" _separator REVERSE)
        string(SUBSTRING "${_rule}" 0 ${_separator} _pattern)
        math(EXPR _value_start "${_separator} + 1")
        string(SUBSTRING "${_rule}" ${_value_start} -1 _rule_kind)
        if("${_name}" MATCHES "${_pattern}")
          set(_kind "${_rule_kind}")
          break()
        endif()
      endforeach()
      _alumbra_quarantine_labels("test ${_name}" "${_issue}" "${_labels}"
        _checked)
      _alumbra_complete_labels("${_checked}" "${_kind}" _completed)
      _alumbra_validate_labels("test ${_name}" "${_completed}")
      set_tests_properties("${_name}" DIRECTORY "${_directory}"
        PROPERTIES LABELS "${_completed}")
    endforeach()
  endforeach()
endfunction()

cmake_language(DEFER DIRECTORY "${PROJECT_SOURCE_DIR}"
  CALL _alumbra_label_registered_tests)
