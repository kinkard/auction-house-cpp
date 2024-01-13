#pragma once

#include "storage.hpp"

struct UserConnection {
  User user;
  std::shared_ptr<Storage> storage;
};

namespace commands {
using Type = std::string (*)(UserConnection &, std::string_view);

std::string ping(UserConnection & connection, std::string_view args);
std::string deposit(UserConnection & connection, std::string_view args);
std::string withdraw(UserConnection & connection, std::string_view args);
std::string view_items(UserConnection & connection, std::string_view);
// std::string sell(UserConnection & connection, std::string_view args);
// std::string buy(UserConnection & connection, std::string_view args);
// std::string view_orders(UserConnection & connection, std::string_view args);

}  // namespace commands
