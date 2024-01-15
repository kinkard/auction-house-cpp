#include "commands.hpp"
#include "shared_state.hpp"
#include "storage.hpp"

#include <asio/awaitable.hpp>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/signal_set.hpp>
#include <asio/write.hpp>

#include <fmt/format.h>

#include <charconv>
#include <cstring>
#include <string_view>

using asio::awaitable;
using asio::co_spawn;
using asio::detached;
using asio::use_awaitable;
using asio::ip::tcp;

awaitable<void> process_user_commands(tcp::socket socket, UserConnection connection) {
  auto shared_socket = std::make_shared<tcp::socket>(std::move(socket));
  connection.shared_state->sockets[connection.user.id] = shared_socket;

  char buffer[256];
  try {
    for (;;) {
      std::size_t n = co_await shared_socket->async_read_some(asio::buffer(buffer), use_awaitable);
      auto response = process_request(connection, { buffer, n });
      co_await async_write(*shared_socket, asio::buffer(response), use_awaitable);
    }
  } catch (std::exception & e) {
    fmt::println("Connection with user {}, id={} was closed by client: {}", connection.user.username,
                 connection.user.id, e.what());
    connection.shared_state->sockets.erase(connection.user.id);
  }
}

awaitable<void> notify_users(std::shared_ptr<SharedState> shared_state) {
  namespace ch = std::chrono;
  auto timer = asio::steady_timer(co_await asio::this_coro::executor, std::chrono::seconds(1));

  for (;;) {
    co_await timer.async_wait(use_awaitable);
    timer.expires_at(timer.expiry() + ch::seconds(1));

    while (!shared_state->notifications.empty()) {
      auto const & [user_id, message] = shared_state->notifications.front();
      auto it = shared_state->sockets.find(user_id);
      if (it != shared_state->sockets.end()) {
        auto const socket = it->second;  // prevent socket from being destroyed while we are writing to it
        try {
          co_await async_write(*socket, asio::buffer(message), use_awaitable);
        } catch (std::exception &) {
          // Just do nothing. User might have disconnected but we still have a socket
        }
      }
      shared_state->notifications.pop();
    }
  }
}

awaitable<void> process_expired_sell_orders(std::shared_ptr<SharedState> shared_state) {
  namespace ch = std::chrono;
  auto timer = asio::steady_timer(co_await asio::this_coro::executor, std::chrono::seconds(1));

  for (;;) {
    co_await timer.async_wait(use_awaitable);
    timer.expires_at(timer.expiry() + ch::seconds(1));

    int64_t const unix_now = std::chrono::seconds(std::time(NULL)).count();
    auto result = shared_state->storage.process_expired_sell_orders(unix_now);
    if (!result) {
      fmt::println("Failed to cancel expired sell orders at {} unix time: {}", unix_now, result.error());
    }
    for (auto const & order : *result) {
      order.save_to(shared_state->transaction_log);
      shared_state->notifications.push(std::make_pair(
          order.seller_id, fmt::format("Your sell order #{} was executed for {}", order.id, order.price)));
    }
  }
}

awaitable<void> process_client_login(tcp::socket socket, std::shared_ptr<SharedState> state) {
  try {
    std::string_view const greeting = "Welcome to Sundris Auction House, stranger! How can I call you?";
    co_await async_write(socket, asio::buffer(greeting), use_awaitable);

    char buffer[256];
    std::size_t n = co_await socket.async_read_some(asio::buffer(buffer), use_awaitable);
    std::string_view const username = { buffer, n };

    auto user = state->storage.get_or_create_user(username).map_error(
        [&](auto && err) { return fmt::format("Failed to login as '{}': {}", username, err); });
    if (!user) {
      co_await async_write(socket, asio::buffer(user.error()), use_awaitable);
      co_return;  // it will close the socket as well
    }

    std::string response = fmt::format("Successfully logged in as {}", user->username);
    co_await async_write(socket, asio::buffer(response), use_awaitable);
    fmt::println("User {}, id={} successfully logged in", user->username, user->id);

    // Spawn a new coroutine to handle the user
    auto connection = UserConnection{ .user = std::move(user.value()), .shared_state = std::move(state) };
    co_spawn(co_await asio::this_coro::executor, process_user_commands(std::move(socket), std::move(connection)),
             detached);
  } catch (std::exception & e) {
    fmt::println("Failed to process client login: {}", e.what());
  }
}

awaitable<void> listener(uint16_t port, std::shared_ptr<SharedState> shared_state) {
  auto executor = co_await asio::this_coro::executor;
  tcp::acceptor acceptor(executor, { tcp::v4(), port });
  fmt::println("Listening on port {}", port);
  for (;;) {
    tcp::socket socket = co_await acceptor.async_accept(use_awaitable);
    co_spawn(executor, process_client_login(std::move(socket), shared_state), detached);
  }
}

int main(int argc, char * argv[]) {
  if (argc != 4) {
    fmt::println("Usage: server <port> <path_to_db> <path_to_transaction_log>");
    fmt::println("Example: server 3000 db.sqlite transaction.log");
    return 1;
  }

  uint16_t port;
  std::from_chars_result result = std::from_chars(argv[1], std::strchr(argv[1], '\0'), port);
  if (result.ec != std::errc()) {
    fmt::println("Invalid argument '{}'. Port must be in range [1, 65535]", argv[1]);
    return 1;
  }

  auto storage = Storage::open(argv[2]);
  if (!storage) {
    fmt::println("Failed to open database: {}", storage.error());
    return 1;
  }
  auto shared_storage = std::make_shared<Storage>(std::move(*storage));

  auto transaction_log = TransactionLog::open(argv[3]);
  if (!transaction_log) {
    fmt::println("Failed to open transaction log: {}", transaction_log.error());
    return 1;
  }

  auto shared_state = std::make_shared<SharedState>(SharedState{
      .storage = std::move(*shared_storage),
      .transaction_log = std::move(*transaction_log),
      .notifications = {},
      .sockets = {},
  });

  try {
    asio::io_context io_context(1);

    // Graceful shutdown
    asio::signal_set signals(io_context, SIGINT, SIGTERM);
    signals.async_wait([&](auto, auto) {
      fmt::println("Shutting down...");
      io_context.stop();
    });

    co_spawn(io_context, listener(port, shared_state), detached);
    co_spawn(io_context, process_expired_sell_orders(shared_state), detached);
    co_spawn(io_context, notify_users(std::move(shared_state)), detached);

    io_context.run();
  } catch (std::exception & e) {
    fmt::println("Exception: {}", e.what());
  }
}
