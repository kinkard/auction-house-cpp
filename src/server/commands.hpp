#pragma once

#include "storage.hpp"

struct UserConnection {
  User user;
  std::shared_ptr<Storage> storage;
};

namespace commands {
std::string ping(UserConnection & connection, std::string_view args);
std::string whoami(UserConnection & connection, std::string_view args);
std::string help(UserConnection & connection, std::string_view args);

std::string deposit(UserConnection & connection, std::string_view args);
std::string withdraw(UserConnection & connection, std::string_view args);
std::string view_items(UserConnection & connection, std::string_view);

std::string sell(UserConnection & connection, std::string_view args);
std::string buy(UserConnection & connection, std::string_view args);
std::string view_sell_orders(UserConnection & connection, std::string_view args);

}  // namespace commands

// Resolves command name and arge from request and invokes it, returning the result
std::string process_request(UserConnection & connection, std::string_view request);
