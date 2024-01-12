add_library(sqlite sqlite3.c)
# target_compile_options(asio INTERFACE -DASIO_STANDALONE)
target_include_directories(sqlite SYSTEM PUBLIC ${CMAKE_CURRENT_LIST_DIR})
