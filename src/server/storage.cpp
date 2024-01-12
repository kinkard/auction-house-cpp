#include "storage.hpp"

#include <sqlite3.h>

#include <stdexcept>
#include <string>

namespace {
class StorageException : public std::runtime_error {
public:
  explicit StorageException(std::string_view msg, int sqlite_err)
      : std::runtime_error(std::string(msg) + std::string(sqlite3_errstr(sqlite_err))) {}

  explicit StorageException(std::string_view msg, char const * sqlite_err)
      : std::runtime_error(std::string(msg) + std::string(sqlite_err)) {}
};

// RAII wrapper for sqlite3_stmt with minimalistic interface
struct SqlQuery {
  sqlite3_stmt * inner;

  SqlQuery(sqlite3 * db, std::string_view sql) {
    int rc = sqlite3_prepare_v2(db, sql.data(), sql.size(), &this->inner, nullptr);
    if (rc != SQLITE_OK) {
      throw StorageException("Failed to prepare SQL statement: ", rc);
    }
  }

  ~SqlQuery() { sqlite3_finalize(this->inner); }

  SqlQuery(SqlQuery const &) = delete;
  SqlQuery & operator=(SqlQuery const &) = delete;
  SqlQuery(SqlQuery &&) = delete;
  SqlQuery & operator=(SqlQuery &&) = delete;

  void bind(int index, std::string_view value) {
    int rc = sqlite3_bind_text(this->inner, index, value.data(), value.size(), SQLITE_STATIC);
    if (rc != SQLITE_OK) {
      throw StorageException("Failed to bind SQL parameters: ", rc);
    }
  }
};
}  // namespace

Storage::Storage(char const * path) {
  int rc = sqlite3_open(path, &this->db);
  if (rc != SQLITE_OK) {
    sqlite3_close(db);
    throw StorageException("Failed to open database: ", rc);
  }

  // Create table if it doesn't exist
  const char * sql =
      "CREATE TABLE IF NOT EXISTS users ("
      "id INTEGER PRIMARY KEY,"
      "username TEXT NOT NULL UNIQUE"
      ");";
  char * err_msg;
  rc = sqlite3_exec(db, sql, nullptr, nullptr, &err_msg);
  if (rc != SQLITE_OK) {
    sqlite3_free(err_msg);
    sqlite3_close(db);
    throw StorageException("Failed to create table: ", err_msg);
  }
}

Storage::~Storage() {
  sqlite3_close(this->db);
}

UserId Storage::get_or_create_user(std::string_view username) {
  {
    SqlQuery insert(this->db, "INSERT OR IGNORE INTO users (username) VALUES (?);");
    insert.bind(1, username);

    int rc = sqlite3_step(insert.inner);
    if (rc != SQLITE_DONE) {
      throw StorageException("Failed to execute SQL statement: ", rc);
    }
  }

  {
    SqlQuery select(this->db, "SELECT id FROM users WHERE username = ?;");
    select.bind(1, username);

    int rc = sqlite3_step(select.inner);
    if (rc != SQLITE_ROW) {
      throw StorageException("Failed to execute SQL statement: ", rc);
    }

    int user_id = sqlite3_column_int(select.inner, 0);
    return { user_id };
  }
}
