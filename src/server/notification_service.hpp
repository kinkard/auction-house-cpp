#pragma once

#include "types.hpp"

#include <queue>

struct ExecutedSellOrder {
  int order_id;
  int price;
};

// Service for sending notifications about executed sell orders
class NotificationService {
  // I wish there was a better way to do this, but asio channels
  // are not suitable for sending notification from one piece of code
  // to another, so we have to use a queue and one periodic task that reads it
  std::queue<std::pair<UserId, ExecutedSellOrder>> notifications;

public:
  void push(UserId user_id, ExecutedSellOrder notification) { notifications.push({ user_id, notification }); }

  bool empty() const { return notifications.empty(); }

  std::pair<UserId, ExecutedSellOrder> pop() {
    auto notification = std::move(notifications.front());
    notifications.pop();
    return notification;
  }
};
