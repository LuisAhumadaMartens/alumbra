include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/AlumbraToolchain.cmake")

function(expect_floor expected)
  alumbra_toolchain_floor_error(_error ${ARGN} CPLUSPLUS 202302L)
  if(expected STREQUAL "accept")
    if(_error)
      message(SEND_ERROR "Unexpected rejection (${ARGN}): ${_error}")
    endif()
  elseif(NOT _error MATCHES "${expected}")
    message(SEND_ERROR
      "Expected rejection matching '${expected}' (${ARGN}), got '${_error}'")
  else()
    message(STATUS "Rejected as expected: ${_error}")
  endif()
endfunction()

expect_floor("requires GCC 15 with libstdc\\+\\+ 15"
  COMPILER_ID GNU COMPILER_VERSION 14.2.0 STDLIB libstdc++ STDLIB_VERSION 14)
expect_floor("requires GCC 15 with libstdc\\+\\+ 15"
  COMPILER_ID GNU COMPILER_VERSION 13.3.0 STDLIB libstdc++ STDLIB_VERSION 13)
expect_floor(accept
  COMPILER_ID GNU COMPILER_VERSION 15.2.0 STDLIB libstdc++ STDLIB_VERSION 15)
expect_floor("requires Clang 21"
  COMPILER_ID Clang COMPILER_VERSION 20.1.8 STDLIB libc++ STDLIB_VERSION 200108)
expect_floor("found Clang 21.1.6 with libstdc\\+\\+ 13"
  COMPILER_ID Clang COMPILER_VERSION 21.1.6 STDLIB libstdc++ STDLIB_VERSION 13)
expect_floor("found Clang 21.1.6 with libc\\+\\+ 200108"
  COMPILER_ID Clang COMPILER_VERSION 21.1.6 STDLIB libc++ STDLIB_VERSION 200108)
expect_floor(accept
  COMPILER_ID Clang COMPILER_VERSION 21.1.6 STDLIB libstdc++ STDLIB_VERSION 15)
expect_floor(accept
  COMPILER_ID Clang COMPILER_VERSION 21.1.6 STDLIB libc++ STDLIB_VERSION 210106)
expect_floor("requires Clang 21 with libc\\+\\+ 21, libstdc\\+\\+ 15 or the MSVC STL 145"
  COMPILER_ID Clang COMPILER_VERSION 20.1.8 STDLIB "MSVC STL"
  STDLIB_VERSION 145)
expect_floor("found Clang 21.1.8 with MSVC STL 143"
  COMPILER_ID Clang COMPILER_VERSION 21.1.8 STDLIB "MSVC STL"
  STDLIB_VERSION 143)
expect_floor("found Clang 21.1.8 with MSVC STL 144"
  COMPILER_ID Clang COMPILER_VERSION 21.1.8 STDLIB "MSVC STL"
  STDLIB_VERSION 144)
expect_floor(accept
  COMPILER_ID Clang COMPILER_VERSION 21.1.8 STDLIB "MSVC STL"
  STDLIB_VERSION 145)
expect_floor(accept
  COMPILER_ID Clang COMPILER_VERSION 22.1.0 STDLIB "MSVC STL"
  STDLIB_VERSION 146)
expect_floor("Unsupported C\\+\\+ compiler MSVC 19.50"
  COMPILER_ID MSVC COMPILER_VERSION 19.50.35717 STDLIB "MSVC STL"
  STDLIB_VERSION 145)
expect_floor("requires Xcode 26.6"
  COMPILER_ID AppleClang COMPILER_VERSION 17.0.0.17000013 STDLIB libc++
  STDLIB_VERSION 190102 APPLE_TOOLCHAIN Xcode APPLE_TOOLCHAIN_VERSION 26.5)
expect_floor(accept
  COMPILER_ID AppleClang COMPILER_VERSION 21.0.0.21000101 STDLIB libc++
  STDLIB_VERSION 210106 APPLE_TOOLCHAIN Xcode APPLE_TOOLCHAIN_VERSION 26.6)
expect_floor(accept
  COMPILER_ID AppleClang COMPILER_VERSION 21.0.0.21000101 STDLIB libc++
  STDLIB_VERSION 210106 APPLE_TOOLCHAIN "Command Line Tools"
  APPLE_TOOLCHAIN_VERSION 26.6.0)
expect_floor("Could not determine the Xcode version"
  COMPILER_ID AppleClang COMPILER_VERSION 21.0.0.21000101 STDLIB libc++
  STDLIB_VERSION 210106)
expect_floor("Unsupported C\\+\\+ compiler"
  COMPILER_ID IntelLLVM COMPILER_VERSION 2026.0.0 STDLIB libstdc++
  STDLIB_VERSION 15)

alumbra_toolchain_floor_error(_error COMPILER_ID GNU COMPILER_VERSION 15.2.0
  STDLIB libstdc++ STDLIB_VERSION 15 CPLUSPLUS 202002L)
if(NOT _error MATCHES "does not compile in C\\+\\+23 mode")
  message(SEND_ERROR "C++20 mode was not rejected: '${_error}'")
endif()
