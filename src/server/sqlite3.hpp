#pragma once

#include <tl/expected.hpp>

#include <string_view>

struct sqlite3;
struct sqlite3_stmt;

// RAII wrapper for sqlite3 database
class Sqlite3 final {
  sqlite3 * db;

  // constructor is private, use `open` instead
  Sqlite3(sqlite3 * db);

public:
  tl::expected<Sqlite3, std::string> static open(char const * path);
  ~Sqlite3();

  // This class cannot be copied, but can be moved
  Sqlite3(Sqlite3 const &) = delete;
  Sqlite3 & operator=(Sqlite3 const &) = delete;
  Sqlite3(Sqlite3 && other) noexcept : db(other.db) { other.db = nullptr; }
  Sqlite3 & operator=(Sqlite3 && other) noexcept {
    // move and swap idiom via local varialbe
    Sqlite3 local = std::move(other);
    std::swap(db, local.db);
    return *this;
  }

  // Executes SQL query that doesn't return any data
  tl::expected<void, std::string> execute(std::string_view sql);

  struct Statement final {
    sqlite3_stmt * inner;

    Statement(sqlite3_stmt * inner) : inner(inner) {}
    ~Statement();

    Statement(Statement const &) = delete;
    Statement & operator=(Statement const &) = delete;
    Statement(Statement && other) noexcept : inner(other.inner) { other.inner = nullptr; }
    Statement & operator=(Statement && other) noexcept {
      // move and swap idiom via local varialbe
      Statement local = std::move(other);
      std::swap(inner, local.inner);
      return *this;
    }

    tl::expected<void, std::string> bind(int index, std::string_view value);
  };
  tl::expected<Statement, std::string> prepare(std::string_view sql);
};
