# disable CTest targets like Nightly, Experimental, etc.
set_property(GLOBAL PROPERTY CTEST_TARGETS_ADDED 1)

include(CTest)

include(${CMAKE_SOURCE_DIR}/deps/googletest.cmake)

add_executable(test-storage storage_tests.cpp ${CMAKE_SOURCE_DIR}/src/server/storage.cpp)
target_include_directories(test-storage PRIVATE ${CMAKE_SOURCE_DIR}/src/server/)
target_link_libraries(test-storage PRIVATE gtest_all sqlite3 fmt::fmt)
add_test(NAME test-storage COMMAND test-storage)
