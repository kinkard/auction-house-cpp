add_library(sqlite3 sqlite3.c)
target_compile_options(sqlite3 PRIVATE)
target_include_directories(sqlite3 PUBLIC ${CMAKE_CURRENT_LIST_DIR})
