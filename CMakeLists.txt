cmake_minimum_required(VERSION 3.16)
project(pp VERSION 1.3.2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

file(GLOB_RECURSE PP_SOURCES CONFIGURE_DEPENDS "src/*.cpp")

if(WIN32)
  list(FILTER PP_SOURCES EXCLUDE REGEX ".*/platform/darwin/.*")
elseif(APPLE)
  list(FILTER PP_SOURCES EXCLUDE REGEX ".*/platform/win/.*")
  # Windows-native modules — Darwin uses platform/darwin/* implementations
  list(FILTER PP_SOURCES EXCLUDE REGEX ".*/core/install\\.cpp$")
  list(FILTER PP_SOURCES EXCLUDE REGEX ".*/core/update\\.cpp$")
  list(FILTER PP_SOURCES EXCLUDE REGEX ".*/core/restart\\.cpp$")
  list(FILTER PP_SOURCES EXCLUDE REGEX ".*/util/editor\\.cpp$")
else()
  message(FATAL_ERROR "Unsupported platform — Windows and macOS only")
endif()

add_executable(pp ${PP_SOURCES})
target_include_directories(pp PRIVATE "src")

if(WIN32)
  target_link_libraries(pp PRIVATE shell32 advapi32 ole32 crypt32 winhttp)
elseif(APPLE)
  target_compile_definitions(pp PRIVATE PP_PLATFORM_DARWIN=1)
  find_library(SECURITY_FRAMEWORK Security REQUIRED)
  find_library(COREFOUNDATION_FRAMEWORK CoreFoundation REQUIRED)
  target_link_libraries(pp PRIVATE ${SECURITY_FRAMEWORK} ${COREFOUNDATION_FRAMEWORK})
endif()
