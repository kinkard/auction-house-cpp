# disable CTest targets like Nightly, Experimental, etc.
set_property(GLOBAL PROPERTY CTEST_TARGETS_ADDED 1)

include(CTest)

include(${CMAKE_SOURCE_DIR}/deps/googletest.cmake)

add_executable(test-storage
  storage_tests.cpp
  ${CMAKE_SOURCE_DIR}/src/server/storage.cpp
  ${CMAKE_SOURCE_DIR}/src/server/sqlite3.cpp
)
target_include_directories(test-storage PRIVATE ${CMAKE_SOURCE_DIR}/src/server/)
target_link_libraries(test-storage PRIVATE gtest_all sqlite3 fmt::fmt tl::expected)
add_test(NAME test-storage COMMAND test-storage)

add_executable(test-commands
  commands_tests.cpp
  ${CMAKE_SOURCE_DIR}/src/server/commands.cpp
  # Just to link without problems
  ${CMAKE_SOURCE_DIR}/src/server/transaction_log.cpp
  ${CMAKE_SOURCE_DIR}/src/server/storage.cpp
  ${CMAKE_SOURCE_DIR}/src/server/sqlite3.cpp
)
target_include_directories(test-commands PRIVATE ${CMAKE_SOURCE_DIR}/src/server/)
target_link_libraries(test-commands PRIVATE gtest_all sqlite3 fmt::fmt tl::expected asio)
add_test(NAME test-commands COMMAND test-commands)

