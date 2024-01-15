#include "transaction_log.hpp"

#include <fmt/format.h>
#include <chrono>
#include <tl/expected.hpp>

#include <cstdio>

tl::expected<TransactionLog, std::string> TransactionLog::open(char const * path) {
  std::FILE * file = std::fopen(path, "a");
  if (!file) {
    return tl::make_unexpected(fmt::format("failed to open transaction log '{}'", path));
  }
  return TransactionLog(file);
}

TransactionLog::~TransactionLog() {
  if (file) {
    std::fclose(file);
  }
}

void TransactionLog::log(int user_id, std::string_view message) {
  namespace ch = std::chrono;
  int64_t const unix_now_ms = ch::duration_cast<ch::milliseconds>(ch::system_clock::now().time_since_epoch()).count();
  auto const timestamp = static_cast<double>(unix_now_ms) / 1000.0;

  auto log_entry = fmt::format("{}: user{{.id={}}} {}\n", timestamp, user_id, message);
  std::fwrite(log_entry.data(), 1, log_entry.size(), file);
  std::fflush(file);
}
