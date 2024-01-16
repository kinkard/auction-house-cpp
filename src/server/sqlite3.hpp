#pragma once

#include <tl/expected.hpp>

#include <optional>
#include <string>
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

    // Executes a prepared statement that doesn't return any data, otherwise returns error
    tl::expected<void, std::string> execute();

    // binds all arguments in a single call
    template <typename... Args>
    tl::expected<void, std::string> bind_all(Args &&... args) {
      return bind_all_impl(1, std::forward<Args>(args)...);
    }

  private:
    tl::expected<void, std::string> bind_all_impl(int) { return {}; }
    template <typename T, typename... Args>
    tl::expected<void, std::string> bind_all_impl(int index, T && value, Args &&... args) {
      auto result = bind(index, std::forward<T>(value));
      if (!result) {
        return result;
      }
      return bind_all_impl(index + 1, std::forward<Args>(args)...);
    }

    // Internal implementation of bind for different types
    tl::expected<void, std::string> bind(int index, std::string_view value);
    tl::expected<void, std::string> bind(int index, int64_t value);
    tl::expected<void, std::string> bind(int index, std::nullopt_t);

    template <typename T>
    tl::expected<void, std::string> bind(int index, const std::optional<T> & value) {
      if (value.has_value()) {
        return bind(index, *value);
      } else {
        return bind(index, std::nullopt);
      }
    }
  };

  // Executes SQL query that doesn't return any data
  tl::expected<void, std::string> execute(std::string_view sql);

  // Executes SQL query with parameters that doesn't return any data
  template <typename... Args>
  tl::expected<void, std::string> execute(std::string_view sql, Args &&... args) {
    auto stmt = prepare(sql);
    if (!stmt) {
      return tl::make_unexpected(std::move(stmt.error()));
    }
    auto result = stmt->bind_all(std::forward<Args>(args)...);
    if (!result) {
      return tl::make_unexpected(std::move(result.error()));
    }
    return stmt->execute();
  }

  // Prepares SQL statement and binds all arguments in a single call. Suitable for queries that return data
  template <typename... Args>
  tl::expected<Statement, std::string> query(std::string_view sql, Args &&... args) {
    auto stmt = prepare(sql);
    if (!stmt) {
      return stmt;
    }
    auto result = stmt->bind_all(std::forward<Args>(args)...);
    if (!result) {
      return tl::make_unexpected(std::move(result.error()));
    }
    return stmt;
  }

  // Returns the last inserted row id. Suitable to get the id after INSERT query
  int last_insert_rowid() const;

  // RAII wrapper for transaction
  class TransactionGuard final {
    Sqlite3 & db;
    bool committed = false;

  public:
    TransactionGuard(Sqlite3 & db) : db(db) {}
    ~TransactionGuard() {
      if (!committed) {
        db.execute("ROLLBACK;");
      }
    }

    TransactionGuard(TransactionGuard const &) = delete;
    TransactionGuard & operator=(TransactionGuard const &) = delete;
    TransactionGuard(TransactionGuard && other) noexcept : db(other.db), committed(other.committed) {
      other.committed = true;
    }
    TransactionGuard & operator=(TransactionGuard && other) noexcept {
      // move and swap idiom via local varialbe
      TransactionGuard local = std::move(other);
      std::swap(db, local.db);
      std::swap(committed, local.committed);
      return *this;
    }

    // Commits the transaction
    tl::expected<void, std::string> commit() {
      if (committed) {
        return tl::make_unexpected("Transaction already committed");
      }
      auto result = db.execute("COMMIT;");
      if (result) {
        committed = true;
      }
      return result;
    }
  };

  // Begins a transaction. If TransactionGuard is destroyed without calling `commit`, the transaction is rolled back
  tl::expected<TransactionGuard, std::string> begin_transaction() {
    auto result = execute("BEGIN;");
    if (!result) {
      return tl::make_unexpected(std::move(result.error()));
    }
    return TransactionGuard(*this);
  }

private:
  // Prepares SQL statement for execution. Use `query` instead
  tl::expected<Statement, std::string> prepare(std::string_view sql);
};
