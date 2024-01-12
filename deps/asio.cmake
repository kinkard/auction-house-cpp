# ASIO requires a system-dependent threading library (like pthread on linux)
find_package(Threads REQUIRED)

add_library(asio INTERFACE)
target_compile_options(asio INTERFACE -DASIO_STANDALONE)
target_include_directories(asio SYSTEM INTERFACE ${CMAKE_CURRENT_LIST_DIR}/asio/asio/include)
target_link_libraries(asio INTERFACE Threads::Threads)
