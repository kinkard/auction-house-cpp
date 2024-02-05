#pragma once

#include "auction_service.hpp"
#include "notification_service.hpp"
#include "storage.hpp"
#include "transaction_log.hpp"
#include "user_service.hpp"

#include <asio/ip/tcp.hpp>

#include <memory>

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

  // Service for sending notifications about executed sell orders
  NotificationService notifications;

  // UserId -> Socket map for sending notifications
  std::unordered_map<UserId, std::shared_ptr<asio::ip::tcp::socket>> sockets;
};
