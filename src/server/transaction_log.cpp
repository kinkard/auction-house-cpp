#include "transaction_log.hpp"

#include <fmt/format.h>
#include <tl/expected.hpp>

#include <chrono>

tl::expected<TransactionLog, std::string> TransactionLog::open(std::string_view path) {
  // todo: ensure that there is a `\0` at the end of the string
  std::FILE * file = std::fopen(path.data(), "a");
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

void TransactionLog::save(UserId user_id, std::string_view operation_name, ItemOperationInfo operation_info) {
  log(user_id,
      fmt::format("{} .item_id={} .quantity={}", operation_name, operation_info.item_id, operation_info.quantity));
}

void TransactionLog::save(SellOrderExecutionInfo const & order_info) {
  log(order_info.seller_id, fmt::format("sold .item_id={} .quantity={} .price={} .order_id={}", order_info.item_id,
                                        order_info.quantity, order_info.price, order_info.id));
  log(order_info.buyer_id, fmt::format("bought .item_id={} .quantity={} .price={} .order_id={}", order_info.item_id,
                                       order_info.quantity, order_info.price, order_info.id));
}
