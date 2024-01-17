add_library(sqlite3 sqlite3.c)

# https://www.sqlite.org/compile.html
target_compile_options(sqlite3 PUBLIC
  -DSQLITE_OMIT_DECLTYPE
  -DSQLITE_OMIT_DEPRECATED
)
target_compile_options(sqlite3 PRIVATE
  -DSQLITE_DQS=0
  -DSQLITE_OMIT_AUTOINIT
  -DSQLITE_DEFAULT_FOREIGN_KEYS=1
  -DHAVE_USLEEP=1
)

target_include_directories(sqlite3 PUBLIC ${CMAKE_CURRENT_LIST_DIR})
