#include "storage.hpp"

#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/signal_set.hpp>
#include <asio/write.hpp>

#include <charconv>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <string_view>

using asio::awaitable;
using asio::co_spawn;
using asio::detached;
using asio::use_awaitable;
using asio::ip::tcp;

std::pair<std::string_view, std::string_view> parse_command(std::string_view request) noexcept {
  std::size_t const space_pos = request.find(' ');
  if (space_pos == std::string_view::npos) {
    return { request, {} };
  }
  return { request.substr(0, space_pos), request.substr(space_pos + 1) };
}

awaitable<void> handle_client(tcp::socket socket, std::shared_ptr<Storage> storage) {
  char buffer[256];
  try {
    // first, validate the greating message
    std::size_t n = co_await socket.async_read_some(asio::buffer(buffer), use_awaitable);
    std::string_view request(buffer, n);

    auto [command, username] = parse_command(request);
    if (command != "login") {
      std::string response = "Failed to login. Expected: login <username>\n";
      co_await async_write(socket, asio::buffer(response), use_awaitable);
      co_return;  // it will close the socket as well
    }

    UserId user_id = storage->get_or_create_user(username);
    std::string response = "Successfully logged in as ";
    response += username;
    co_await async_write(socket, asio::buffer(response), use_awaitable);
    std::printf("User %.*s logged in with id %d\n", static_cast<int>(username.size()), username.data(), user_id.id);

    // After that, we will echo everything back
    for (;;) {
      std::size_t n = co_await socket.async_read_some(asio::buffer(buffer), use_awaitable);
      co_await async_write(socket, asio::buffer(buffer, n), use_awaitable);
    }
  } catch (std::exception & e) {
    std::printf("echo Exception: %s\n", e.what());
  }
}

awaitable<void> listener(uint16_t port, std::shared_ptr<Storage> storage) {
  auto executor = co_await asio::this_coro::executor;
  tcp::acceptor acceptor(executor, { tcp::v4(), port });
  std::printf("Listening on port %u\n", port);
  for (;;) {
    tcp::socket socket = co_await acceptor.async_accept(use_awaitable);
    co_spawn(executor, handle_client(std::move(socket), storage), detached);
  }
}

int main(int argc, char * argv[]) {
  if (argc != 3) {
    std::printf("Usage: server <port> <path_to_db>\n");
    std::printf("Example: server 3000 db.sqlite\n");
    return 1;
  }

  uint16_t port;
  std::from_chars_result result = std::from_chars(argv[1], argv[1] + strlen(argv[1]), port);
  if (result.ec != std::errc()) {
    std::printf("Invalid argument '%s'. Port must be in range [1, 65535]\n", argv[1]);
    return 1;
  }

  auto storage = std::make_shared<Storage>(argv[2]);

  try {
    asio::io_context io_context(1);

    // Graceful shutdown
    asio::signal_set signals(io_context, SIGINT, SIGTERM);
    signals.async_wait([&](auto, auto) {
      printf("Shutting down...\n");
      io_context.stop();
    });

    co_spawn(io_context, listener(port, std::move(storage)), detached);

    io_context.run();
  } catch (std::exception & e) {
    std::printf("Exception: %s\n", e.what());
  }
}
