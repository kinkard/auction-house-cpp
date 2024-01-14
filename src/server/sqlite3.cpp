#include "sqlite3.hpp"

#include <sqlite3.h>

#include <fmt/format.h>

Sqlite3::Sqlite3(sqlite3 * db) : db(db) {}
Sqlite3::~Sqlite3() {
  sqlite3_close_v2(db);
}

tl::expected<Sqlite3, std::string> Sqlite3::open(char const * path) {
  sqlite3 * db;
  int rc = sqlite3_open(path, &db);
  if (rc != SQLITE_OK) {
    return tl::make_unexpected(fmt::format("Failed to open database: {}", sqlite3_errstr(rc)));
  }
  return Sqlite3(db);
}

tl::expected<void, std::string> Sqlite3::execute(std::string_view sql) {
  char * err_msg;
  int rc = sqlite3_exec(this->db, sql.data(), nullptr, nullptr, &err_msg);
  if (rc != SQLITE_OK) {
    auto error = fmt::format("Failed to execute SQL: {}", err_msg);
    sqlite3_free(err_msg);
    return tl::make_unexpected(std::move(error));
  }
  return {};
}

tl::expected<Sqlite3::Statement, std::string> Sqlite3::prepare(std::string_view sql) {
  sqlite3_stmt * stmt;
  int rc = sqlite3_prepare_v2(this->db, sql.data(), sql.size(), &stmt, nullptr);
  if (rc != SQLITE_OK) {
    return tl::make_unexpected(fmt::format("Failed to prepare SQL statement: {}", sqlite3_errmsg(this->db)));
  }
  return Statement(stmt);
}

Sqlite3::Statement::~Statement() {
  sqlite3_finalize(this->inner);
}

tl::expected<void, std::string> Sqlite3::Statement::execute() {
  int rc = sqlite3_step(this->inner);
  if (rc != SQLITE_DONE) {
    return tl::make_unexpected(fmt::format("Failed to execute SQL statement: {}", sqlite3_errstr(rc)));
  }
  return {};
}

tl::expected<void, std::string> Sqlite3::Statement::bind(int index, std::string_view value) {
  int rc = sqlite3_bind_text(this->inner, index, value.data(), value.size(), SQLITE_STATIC);
  if (rc != SQLITE_OK) {
    return tl::make_unexpected(fmt::format("Failed to bind SQL parameters: {}", sqlite3_errstr(rc)));
  }
  return {};
}

tl::expected<void, std::string> Sqlite3::Statement::bind(int index, int64_t value) {
  int rc = sqlite3_bind_int64(this->inner, index, value);
  if (rc != SQLITE_OK) {
    return tl::make_unexpected(fmt::format("Failed to bind SQL parameters: {}", sqlite3_errstr(rc)));
  }
  return {};
}

tl::expected<void, std::string> Sqlite3::Statement::bind(int index, std::nullopt_t) {
  int rc = sqlite3_bind_null(this->inner, index);
  if (rc != SQLITE_OK) {
    return tl::make_unexpected(fmt::format("Failed to bind SQL parameters: {}", sqlite3_errstr(rc)));
  }
  return {};
}
