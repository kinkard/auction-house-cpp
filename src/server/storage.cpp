#include "storage.hpp"

#include <sqlite3.h>

#include <stdexcept>
#include <string>

class StorageException : public std::runtime_error {
public:
  explicit StorageException(std::string_view msg, int sqlite_err)
      : std::runtime_error(std::string(msg) + std::string(sqlite3_errstr(sqlite_err))) {}

  explicit StorageException(std::string_view msg, char const * sqlite_err)
      : std::runtime_error(std::string(msg) + std::string(sqlite_err)) {}
};

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
  sqlite3_stmt * stmt;
  int rc = sqlite3_prepare_v2(db, "INSERT OR IGNORE INTO users (username) VALUES (?);", -1, &stmt, nullptr);
  if (rc != SQLITE_OK) {
    throw StorageException("Failed to prepare SQL statement: ", rc);
  }

  rc = sqlite3_bind_text(stmt, 1, username.data(), username.size(), SQLITE_STATIC);
  if (rc != SQLITE_OK) {
    throw StorageException("Failed to bind SQL parameters: ", rc);
  }

  rc = sqlite3_step(stmt);
  if (rc != SQLITE_DONE) {
    throw StorageException("Failed to execute SQL statement: ", rc);
  }
  sqlite3_finalize(stmt);

  rc = sqlite3_prepare_v2(db, "SELECT id FROM users WHERE username = ?;", -1, &stmt, nullptr);
  if (rc != SQLITE_OK) {
    throw StorageException("Failed to prepare SQL statement: ", rc);
  }

  rc = sqlite3_bind_text(stmt, 1, username.data(), username.size(), SQLITE_STATIC);
  if (rc != SQLITE_OK) {
    throw StorageException("Failed to bind SQL parameters: ", rc);
  }

  rc = sqlite3_step(stmt);
  if (rc != SQLITE_ROW) {
    throw StorageException("Failed to execute SQL statement: ", rc);
  }

  int user_id = sqlite3_column_int(stmt, 0);
  sqlite3_finalize(stmt);

  return { user_id };
}
