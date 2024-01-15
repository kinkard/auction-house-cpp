cmake_minimum_required(VERSION 3.22.0 FATAL_ERROR)
project(auction-house LANGUAGES CXX C)

# Compiler configuration
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Dependencies
include(deps/asio.cmake)
add_subdirectory(deps/sqlite3)
add_subdirectory(deps/fmt)
set(EXPECTED_BUILD_TESTS OFF)
add_subdirectory(deps/expected)

# Clang warning documentation - https://clang.llvm.org/docs/DiagnosticsReference.html
# GCC warning documentation - https://gcc.gnu.org/onlinedocs/gcc/Warning-Options.html
# Visual Studio warning documentation - https://learn.microsoft.com/en-us/cpp/build/reference/compiler-option-warning-level
if(MSVC)
  set(COMPILE_FLAGS /WX /Wall)
else()
  include(CheckCXXCompilerFlag)
  set(COMPILE_FLAGS -Werror -Wall -Wextra -pedantic -pedantic-errors)
  set(EXTRA_COMPILE_FLAGS
    -Wcast-align
    -Wcast-qual
    -Wcomma
    -Wduplicated-cond
    -Wextra-semi
    -Wfloat-equal
    -Wlogical-op
    -Wnewline-eof
    -Wno-dangling-else
    -Wnon-virtual-dtor
    -Woverloaded-virtual
    -Wredundant-decls
    -Wsuggest-override
    -Wunreachable-code
    -Wconversion
  )
  foreach(extra_flag ${EXTRA_COMPILE_FLAGS})
    check_cxx_compiler_flag(${extra_flag} HAS_FLAG_${extra_flag})
    if(HAS_FLAG_${extra_flag})
        set(COMPILE_FLAGS ${COMPILE_FLAGS} ${extra_flag})
    endif()
  endforeach()
endif()

# Server
add_executable(server 
  src/server/main.cpp
  src/server/storage.cpp
  src/server/sqlite3.cpp
  src/server/commands.cpp
  src/server/transaction_log.cpp
)
target_link_libraries(server asio sqlite3 fmt::fmt tl::expected)
target_compile_options(server PRIVATE ${COMPILE_FLAGS})

# Client
add_executable(client src/client/main.cpp)
target_link_libraries(client asio)
target_compile_options(client PRIVATE ${COMPILE_FLAGS})

add_subdirectory(tests)
