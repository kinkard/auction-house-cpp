#pragma once

#include "sqlite3.hpp"
#include "types.hpp"

#include <fmt/format.h>
#include <optional>
#include <string_view>

// Wrapper around sqlite3 database with core business logic
class Storage final {
  Sqlite3 _db;
  int _funds_item_id;

  // Store funds as an item for simplicity in `deposit` and `withdraw` operations
  static constexpr std::string_view FUNDS_ITEM_NAME = "funds";

  // constructor is private, use `open` instead
  Storage(Sqlite3 && db, int funds_item_id) noexcept : _db(std::move(db)), _funds_item_id(funds_item_id) {}

public:
  // Opens a database file. If the file doesn't exist, it will be created
  tl::expected<Storage, std::string> static open(std::string_view path);
  ~Storage() = default;

  // This class cannot be copied, but can be moved
  Storage(Storage const &) = delete;
  Storage & operator=(Storage const &) = delete;
  Storage(Storage &&) = default;
  Storage & operator=(Storage &&) = default;

  // Funds are stored in a special item with the given name
  std::string_view funds_item_name() const { return FUNDS_ITEM_NAME; }
  int funds_item_id() const { return _funds_item_id; }

  // Returns the user id by username if exists. std::nullopt otherwise
  std::optional<UserId> get_user_id(std::string_view username);

  // Creates a new user with the given username. Returns the user id if the user was created successfully
  tl::expected<UserId, std::string> create_user(std::string_view username);

  // Creates a new item with the given name. Returns the item id if the item was created successfully
  tl::expected<int, std::string> create_item(std::string_view item_name);

  // Returns the item id by name if exists
  tl::expected<int, std::string> get_item_id(std::string_view item_name);

  // Add or subtract the quantity of the item for the user
  tl::expected<void, std::string> add_user_item(UserId user_id, int item_id, int quantity);
  tl::expected<void, std::string> sub_user_item(UserId user_id, int item_id, int quantity);

  // Returns the quantity of the item for the user. std::nullopt can be treated as 0
  std::optional<int> get_user_items_quantity(UserId user_id, int item_id);

  // List all user items
  tl::expected<std::vector<std::pair<std::string, int>>, std::string> view_user_items(UserId user_id);

  struct SellOrder {
    UserId seller_id;
    int item_id;
    int quantity;
    int price;
    int64_t unix_expiration_time;

    // Stores an information about the order type and state:
    // - For immediate orders, buyer_id is equal to the seller_id
    // - For auction orders, buyer_id is null untill someone places a bid
    std::optional<UserId> buyer_id;
  };
  tl::expected<void, std::string> create_sell_order(SellOrder order);

  tl::expected<void, std::string> delete_sell_order(int order_id);

  tl::expected<void, std::string> update_sell_order_buyer(int order_id, UserId buyer_id, int price);

  // Inner struct that represents a sell order
  struct SellOrderInnerInfo {
    int seller_id;
    int item_id;
    int quantity;
    int price;
    std::optional<int> buyer_id;

    SellOrderType type() const { return buyer_id == seller_id ? SellOrderType::Immediate : SellOrderType::Auction; }
  };
  std::optional<SellOrderInnerInfo> get_sell_order_info(int sell_order_id);

  // View all sell orders
  tl::expected<std::vector<SellOrderInfo>, std::string> view_sell_orders();

  // Cancel expired sell orders
  tl::expected<std::vector<SellOrderExecutionInfo>, std::string> process_expired_sell_orders(int64_t unix_now);

  // RAII wrapper for transaction that will execute Storage::rollback_transaction() on destruction if
  // TransactionGuard::commit() wasn't called
  class TransactionGuard final {
    // pointer to the storage is used to check if the transaction is still active
    Storage * storage;

  public:
    TransactionGuard(Storage * storage) : storage(storage) {}
    ~TransactionGuard() {
      if (storage) {
        storage->rollback_transaction();
      }
    }

    TransactionGuard(TransactionGuard const &) = delete;
    TransactionGuard & operator=(TransactionGuard const &) = delete;
    TransactionGuard(TransactionGuard && other) noexcept : storage(other.storage) { other.storage = nullptr; }
    TransactionGuard & operator=(TransactionGuard && other) noexcept {
      // move and swap idiom via local varialbe
      TransactionGuard local = std::move(other);
      std::swap(storage, local.storage);
      return *this;
    }

    // Commits the transaction
    tl::expected<void, std::string> commit() {
      if (!storage) {
        return tl::make_unexpected("Transaction already committed");
      }
      auto result = storage->commit_transaction();
      if (result) {
        storage = nullptr;
      }
      return result;
    }
  };

  // Begins a transaction. If TransactionGuard is destroyed without calling `commit`, the transaction is rolled back
  tl::expected<TransactionGuard, std::string> begin_transaction();

private:
  void rollback_transaction();
  tl::expected<void, std::string> commit_transaction();
};
