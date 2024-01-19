#pragma once

#include "storage.hpp"
#include "transaction_log.hpp"

#include <asio/ip/tcp.hpp>

#include <memory>
#include <queue>

// This is a shared state between all users and all commands
struct SharedState {
  // Persistent storage for users and items
  Storage storage;
  // Transaction log for all operations with items
  TransactionLog transaction_log;

  // I wish there was a better way to do this, but asio channels
  // are not suitable for sending notification from one piece of code
  // to another, so we have to use a queue and one periodic task that reads it
  std::queue<std::pair<UserId, std::string>> notifications;
  // UserId -> Socket map for sending notifications
  std::unordered_map<UserId, std::shared_ptr<asio::ip::tcp::socket>> sockets;
};
