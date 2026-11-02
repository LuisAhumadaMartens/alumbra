set(_alumbra_toolchain_dir "${CMAKE_CURRENT_LIST_DIR}")

function(_alumbra_apple_toolchain out_name out_version)
  set(_name "")
  set(_version "")
  file(REAL_PATH "${CMAKE_CXX_COMPILER}" _compiler)
  if(NOT _compiler MATCHES
      "\\.app/Contents/Developer/|^/Library/Developer/CommandLineTools/")
    execute_process(COMMAND xcrun --find clang++
      OUTPUT_VARIABLE _found OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    if(_found)
      file(REAL_PATH "${_found}" _compiler)
    endif()
  endif()
  if(_compiler MATCHES "^(.*\\.app)/Contents/Developer/")
    set(_plist "${CMAKE_MATCH_1}/Contents/version.plist")
    if(EXISTS "${_plist}")
      file(READ "${_plist}" _content)
      if(_content MATCHES
          "<key>CFBundleShortVersionString</key>[ \t\r\n]*<string>([^<]+)</string>")
        set(_name "Xcode")
        set(_version "${CMAKE_MATCH_1}")
      endif()
    endif()
  elseif(_compiler MATCHES "^/Library/Developer/CommandLineTools/")
    execute_process(
      COMMAND pkgutil --pkg-info=com.apple.pkg.CLTools_Executables
      OUTPUT_VARIABLE _info ERROR_QUIET)
    if(_info MATCHES "version: ([0-9]+\\.[0-9]+(\\.[0-9]+)?)")
      set(_name "Command Line Tools")
      set(_version "${CMAKE_MATCH_1}")
    endif()
  endif()
  set(${out_name} "${_name}" PARENT_SCOPE)
  set(${out_version} "${_version}" PARENT_SCOPE)
endfunction()

function(alumbra_detect_toolchain prefix)
  set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
  set(_info_file "${CMAKE_BINARY_DIR}/CMakeFiles/alumbra-toolchain-info.a")
  try_compile(_compiled
    SOURCES "${_alumbra_toolchain_dir}/toolchain_info.cc"
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
    COPY_FILE "${_info_file}"
    OUTPUT_VARIABLE _output)
  if(NOT _compiled)
    message(FATAL_ERROR
      "The C++ compiler could not build a C++23 translation unit:\n${_output}")
  endif()
  file(STRINGS "${_info_file}" _info REGEX "ALUMBRA_TOOLCHAIN_INFO\\|")
  foreach(_key IN ITEMS stdlib stdlib_version cplusplus min_os)
    string(REGEX MATCH "\\|${_key}=([^|]*)\\|" _match "${_info}")
    string(TOUPPER "${_key}" _variable)
    set(_${_variable} "${CMAKE_MATCH_1}")
  endforeach()
  set(_apple_name "")
  set(_apple_version "")
  if(CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang")
    _alumbra_apple_toolchain(_apple_name _apple_version)
  endif()
  set(_summary "${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
  if(_apple_name)
    string(APPEND _summary " (${_apple_name} ${_apple_version})")
  endif()
  string(APPEND _summary
    ", ${_STDLIB} ${_STDLIB_VERSION}, __cplusplus ${_CPLUSPLUS}")
  set(${prefix}_STDLIB "${_STDLIB}" PARENT_SCOPE)
  set(${prefix}_STDLIB_VERSION "${_STDLIB_VERSION}" PARENT_SCOPE)
  set(${prefix}_CPLUSPLUS "${_CPLUSPLUS}" PARENT_SCOPE)
  set(${prefix}_MIN_OS "${_MIN_OS}" PARENT_SCOPE)
  set(${prefix}_APPLE_TOOLCHAIN "${_apple_name}" PARENT_SCOPE)
  set(${prefix}_APPLE_TOOLCHAIN_VERSION "${_apple_version}" PARENT_SCOPE)
  set(${prefix}_SUMMARY "${_summary}" PARENT_SCOPE)
endfunction()

function(alumbra_toolchain_floor_error out_var)
  set(_keywords COMPILER_ID COMPILER_VERSION STDLIB STDLIB_VERSION
    APPLE_TOOLCHAIN APPLE_TOOLCHAIN_VERSION CPLUSPLUS)
  cmake_parse_arguments(PARSE_ARGV 1 _arg "" "${_keywords}" "")
  set(_stdlib_major 0)
  if(_arg_STDLIB STREQUAL "libc++" AND _arg_STDLIB_VERSION MATCHES "^[0-9]+$")
    if(_arg_STDLIB_VERSION GREATER_EQUAL 100000)
      math(EXPR _stdlib_major "${_arg_STDLIB_VERSION} / 10000")
    else()
      math(EXPR _stdlib_major "${_arg_STDLIB_VERSION} / 1000")
    endif()
  elseif(_arg_STDLIB MATCHES "^(libstdc\\+\\+|MSVC STL)$"
      AND _arg_STDLIB_VERSION MATCHES "^[0-9]+$")
    set(_stdlib_major "${_arg_STDLIB_VERSION}")
  endif()
  set(_found "${_arg_COMPILER_ID} ${_arg_COMPILER_VERSION}")
  if(_arg_APPLE_TOOLCHAIN)
    string(APPEND _found
      " (${_arg_APPLE_TOOLCHAIN} ${_arg_APPLE_TOOLCHAIN_VERSION})")
  endif()
  string(APPEND _found " with ${_arg_STDLIB} ${_arg_STDLIB_VERSION}")

  set(_below FALSE)
  if(_arg_COMPILER_ID STREQUAL "GNU")
    set(_floor "GCC 15 with libstdc++ 15")
    if(_arg_COMPILER_VERSION VERSION_LESS 15
        OR NOT _arg_STDLIB STREQUAL "libstdc++" OR _stdlib_major LESS 15)
      set(_below TRUE)
    endif()
  elseif(_arg_COMPILER_ID STREQUAL "Clang")
    set(_floor
      "Clang 21 with libc++ 21, libstdc++ 15 or the MSVC STL 145 (clang-cl)")
    if(_arg_COMPILER_VERSION VERSION_LESS 21)
      set(_below TRUE)
    elseif(_arg_STDLIB STREQUAL "libc++")
      if(_stdlib_major LESS 21)
        set(_below TRUE)
      endif()
    elseif(_arg_STDLIB STREQUAL "libstdc++")
      if(_stdlib_major LESS 15)
        set(_below TRUE)
      endif()
    elseif(_arg_STDLIB STREQUAL "MSVC STL")
      if(_stdlib_major LESS 145)
        set(_below TRUE)
      endif()
    else()
      set(_below TRUE)
    endif()
  elseif(_arg_COMPILER_ID STREQUAL "AppleClang")
    set(_floor "Xcode 26.6 with its Apple libc++")
    if(NOT _arg_APPLE_TOOLCHAIN_VERSION)
      string(CONCAT _message
        "Could not determine the Xcode version of ${_arg_COMPILER_ID} "
        "${_arg_COMPILER_VERSION}; ADR-0013 requires ${_floor} or newer. "
        "Select an Xcode with xcode-select or DEVELOPER_DIR.")
      set(${out_var} "${_message}" PARENT_SCOPE)
      return()
    endif()
    if(_arg_APPLE_TOOLCHAIN_VERSION VERSION_LESS 26.6
        OR NOT _arg_STDLIB STREQUAL "libc++")
      set(_below TRUE)
    endif()
  else()
    string(CONCAT _message
      "Unsupported C++ compiler ${_found}. ADR-0013 floors: GCC 15 with "
      "libstdc++ 15, Clang 21 with libc++ 21, libstdc++ 15 or the MSVC STL 145 "
      "(clang-cl), or Xcode 26.6.")
    set(${out_var} "${_message}" PARENT_SCOPE)
    return()
  endif()

  string(REGEX REPLACE "L$" "" _cplusplus "${_arg_CPLUSPLUS}")
  set(_message "")
  if(_below)
    string(CONCAT _message
      "Toolchain below the ADR-0013 floor: requires ${_floor} or newer; "
      "found ${_found}.")
  elseif(NOT _cplusplus MATCHES "^[0-9]+$" OR _cplusplus LESS 202302)
    string(CONCAT _message
      "${_found} does not compile in C++23 mode (__cplusplus is "
      "${_arg_CPLUSPLUS}, expected 202302L or newer).")
  endif()
  set(${out_var} "${_message}" PARENT_SCOPE)
endfunction()

function(alumbra_check_toolchain)
  alumbra_detect_toolchain(_toolchain)
  message(STATUS "Alumbra toolchain: ${_toolchain_SUMMARY}")
  alumbra_toolchain_floor_error(_error
    COMPILER_ID "${CMAKE_CXX_COMPILER_ID}"
    COMPILER_VERSION "${CMAKE_CXX_COMPILER_VERSION}"
    STDLIB "${_toolchain_STDLIB}"
    STDLIB_VERSION "${_toolchain_STDLIB_VERSION}"
    APPLE_TOOLCHAIN "${_toolchain_APPLE_TOOLCHAIN}"
    APPLE_TOOLCHAIN_VERSION "${_toolchain_APPLE_TOOLCHAIN_VERSION}"
    CPLUSPLUS "${_toolchain_CPLUSPLUS}")
  if(_error)
    message(FATAL_ERROR "${_error}")
  endif()
endfunction()
