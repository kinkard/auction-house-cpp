#include "storage.hpp"

#include <sqlite3.h>

#include <fmt/format.h>

#include <stdexcept>
#include <string>

tl::expected<Storage, std::string> Storage::open(const char * path) {
  auto db = Sqlite3::open(path);
  if (!db) {
    return tl::make_unexpected(fmt::format("Failed to open database: {}", db.error()));
  }

  auto result = db->execute(
      "CREATE TABLE IF NOT EXISTS users ("
      "id INTEGER PRIMARY KEY,"
      "username TEXT NOT NULL UNIQUE"
      ");");
  if (!result) {
    return tl::make_unexpected(fmt::format("Failed to create 'users' table: {}", result.error()));
  }

  return Storage(std::move(*db));
}

tl::expected<UserId, std::string> Storage::get_or_create_user(std::string_view username) {
  auto insert = this->db.prepare("INSERT OR IGNORE INTO users (username) VALUES (?1);").and_then([&](auto insert) {
    return insert.bind(1, username).and_then([&]() -> tl::expected<void, std::string> {
      int rc = sqlite3_step(insert.inner);
      if (rc != SQLITE_DONE) {
        return tl::make_unexpected(fmt::format("Failed to execute SQL statement: {}", sqlite3_errstr(rc)));
      }
      return {};
    });
  });
  if (!insert) {
    return tl::make_unexpected(insert.error());
  }

  auto select = this->db.prepare("SELECT id FROM users WHERE username = ?1;").and_then([&](auto select) {
    return select.bind(1, username).and_then([&]() -> tl::expected<UserId, std::string> {
      int rc = sqlite3_step(select.inner);
      if (rc != SQLITE_ROW) {
        throw std::runtime_error(fmt::format("Failed to execute SQL statement: {}", sqlite3_errstr(rc)));
      }

      int user_id = sqlite3_column_int(select.inner, 0);
      return UserId{ user_id };
    });
  });
  return select;
}
