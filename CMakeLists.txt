cmake_minimum_required(VERSION 3.16)
project(pp VERSION 1.3.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

file(GLOB_RECURSE PP_SOURCES CONFIGURE_DEPENDS "src/*.cpp")

add_executable(pp ${PP_SOURCES})
target_include_directories(pp PRIVATE "src")

if(WIN32)
  target_link_libraries(pp PRIVATE shell32 advapi32 ole32 crypt32 winhttp)
endif()
