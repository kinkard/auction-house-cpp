#pragma once

#include <tl/expected.hpp>

#include <string_view>
#include <utility>

// Stateless wrapper around C-style file api, that performs append-only writes,
// so it is safe to use from multiple threads.
class TransactionLog final {
  std::FILE * file;

  // private constructor, use `open` instead
  TransactionLog(std::FILE * file) : file(file) {}

public:
  static tl::expected<TransactionLog, std::string> open(char const * path);
  ~TransactionLog();

  // This class cannot be copied, but can be moved
  TransactionLog(TransactionLog const &) = delete;
  TransactionLog & operator=(TransactionLog const &) = delete;
  TransactionLog(TransactionLog && other) noexcept : file(other.file) { other.file = nullptr; }
  TransactionLog & operator=(TransactionLog && other) noexcept {
    // move and swap idiom via local varialbe
    TransactionLog local = std::move(other);
    std::swap(file, local.file);
    return *this;
  }

  // sales, deposits and withdrawals
  void log(int user_id, std::string_view message);
};