#include "commands.hpp"
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
  char buffer[256];
  try {
    for (;;) {
      std::size_t n = co_await socket.async_read_some(asio::buffer(buffer), use_awaitable);
      auto response = process_request(connection, { buffer, n });
      co_await async_write(socket, asio::buffer(response), use_awaitable);
    }
  } catch (std::exception & e) {
    fmt::println("Connection with user {}, id={} was closed by client: {}", connection.user.username,
                 connection.user.id, e.what());
  }
}

awaitable<void> process_expired_sell_orders(std::shared_ptr<Storage> storage) {
  namespace ch = std::chrono;
  auto timer = asio::steady_timer(co_await asio::this_coro::executor, std::chrono::seconds(1));

  for (;;) {
    co_await timer.async_wait(use_awaitable);
    timer.expires_at(timer.expiry() + ch::seconds(1));

    int64_t const unix_now = std::chrono::seconds(std::time(NULL)).count();
    auto result = storage->process_expired_sell_orders(unix_now);
    if (!result) {
      fmt::println("Failed to cancel expired sell orders at {} unix time: {}", unix_now, result.error());
    }
  }
}

awaitable<void> process_client_login(tcp::socket socket, std::shared_ptr<Storage> storage) {
  try {
    std::string_view const greeting = "Welcome to Sundris Auction House, stranger! How can I call you?";
    co_await async_write(socket, asio::buffer(greeting), use_awaitable);

    char buffer[256];
    std::size_t n = co_await socket.async_read_some(asio::buffer(buffer), use_awaitable);
    std::string_view const username = { buffer, n };

    auto user = storage->get_or_create_user(username).map_error(
        [&](auto && err) { return fmt::format("Failed to login as '{}': {}", username, err); });
    if (!user) {
      co_await async_write(socket, asio::buffer(user.error()), use_awaitable);
      co_return;  // it will close the socket as well
    }

    std::string response = fmt::format("Successfully logged in as {}", user->username);
    co_await async_write(socket, asio::buffer(response), use_awaitable);
    fmt::println("User {}, id={} successfully logged in", user->username, user->id);

    // Spawn a new coroutine to handle the user
    co_spawn(co_await asio::this_coro::executor,
             process_user_commands(std::move(socket),
                                   UserConnection{ .user = std::move(*user), .storage = std::move(storage) }),
             detached);
  } catch (std::exception & e) {
    fmt::println("Failed to process client login: {}", e.what());
  }
}

awaitable<void> listener(uint16_t port, std::shared_ptr<Storage> storage) {
  auto executor = co_await asio::this_coro::executor;
  tcp::acceptor acceptor(executor, { tcp::v4(), port });
  fmt::println("Listening on port {}", port);
  for (;;) {
    tcp::socket socket = co_await acceptor.async_accept(use_awaitable);
    co_spawn(executor, process_client_login(std::move(socket), storage), detached);
  }
}

int main(int argc, char * argv[]) {
  if (argc != 3) {
    fmt::println("Usage: server <port> <path_to_db>");
    fmt::println("Example: server 3000 db.sqlite");
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

  try {
    auto shared_storage = std::make_shared<Storage>(std::move(*storage));

    asio::io_context io_context(1);

    // Graceful shutdown
    asio::signal_set signals(io_context, SIGINT, SIGTERM);
    signals.async_wait([&](auto, auto) {
      fmt::println("Shutting down...");
      io_context.stop();
    });

    co_spawn(io_context, listener(port, shared_storage), detached);
    co_spawn(io_context, process_expired_sell_orders(std::move(shared_storage)), detached);

    io_context.run();
  } catch (std::exception & e) {
    fmt::println("Exception: {}", e.what());
  }
}
