#pragma once

#include "auction_service.hpp"
#include "storage.hpp"
#include "transaction_log.hpp"
#include "user_service.hpp"

#include <asio/ip/tcp.hpp>

#include <memory>
#include <queue>

// Shared state between all users and items
struct SharedState {
  // Persistent storage for users and items
  std::shared_ptr<Storage> storage;

  // Core logic for all operations with items
  AuctionService auction_service;

  // Core logic for all operations with users
  UserService user_service;

  // Transaction log for all operations with items
  TransactionLog transaction_log;

  // I wish there was a better way to do this, but asio channels
  // are not suitable for sending notification from one piece of code
  // to another, so we have to use a queue and one periodic task that reads it
  std::queue<std::pair<UserId, std::string>> notifications;
  // UserId -> Socket map for sending notifications
  std::unordered_map<UserId, std::shared_ptr<asio::ip::tcp::socket>> sockets;
};
