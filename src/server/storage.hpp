#pragma once

#include "sqlite3.hpp"
#include "tl/expected.hpp"

using UserId = int;

struct User {
  UserId id;
  std::string username;
};

class Storage final {
  Sqlite3 db;
  int funds_item_id;

  // constructor is private, use `open` instead
  Storage(Sqlite3 && db, int funds_item_id) noexcept : db(std::move(db)), funds_item_id(funds_item_id){};

public:
  tl::expected<Storage, std::string> static open(char const * path);
  ~Storage() = default;

  // This class cannot be copied, but can be moved
  Storage(Storage const &) = delete;
  Storage & operator=(Storage const &) = delete;
  Storage(Storage &&) = default;
  Storage & operator=(Storage &&) = default;

  // Returns user id of the already existing or newly created user
  tl::expected<User, std::string> get_or_create_user(std::string_view username);

  // Deposint item to the user. "funds" item is used to store the balance
  tl::expected<void, std::string> deposit(UserId user_id, std::string_view item_name, int count);

  // Withdraws item from the user. "funds" item is used to store the balance
  tl::expected<void, std::string> withdraw(UserId user_id, std::string_view item_name, int count);

  // List all user items
  tl::expected<std::vector<std::pair<std::string, int>>, std::string> view_items(UserId user_id);

private:
  bool is_valid_user(UserId user_id);
};
