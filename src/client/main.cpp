#include <asio/awaitable.hpp>
#include <asio/co_spawn.hpp>
#include <asio/connect.hpp>
#include <asio/detached.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/signal_set.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/write.hpp>

#include <iostream>
#include <optional>
#include <string_view>
#include <thread>

using asio::awaitable;
using asio::co_spawn;
using asio::detached;
using asio::use_awaitable;
using asio::ip::tcp;

// Parses "<hostname>:<port>" into a pair of strings.
std::optional<std::pair<std::string, std::string>> parse_hostname_port(std::string_view str) {
  auto const colon_pos = str.find(':');
  if (colon_pos == std::string_view::npos) {
    return std::nullopt;
  }

  std::string hostname(str.substr(0, colon_pos));
  std::string port(str.substr(colon_pos + 1));
  return std::make_pair(std::move(hostname), std::move(port));
}

// Spawns a thread that reads commands from stdin and sends them to the server. A separate thread is
// used because unfortunately `asio` doesn't provide a way to read from stdin asynchronously. So we
// use blocking `std::getline` on a separate thread to avoid blocking the async runtime.
//
// Reasoning for such a simplistic implementation:
// - thread lifetime - this thread will be aborted when the main thread exits
// - socket lifetime - socket should live until the end of the main thread to avoid lifetime issues
// - exceptions - if an exception is thrown because socket is closed (or whatever else),
//   this thread will be aborted, which is not a problem because the main thread will be aborted as well
//   on the same exception in the task that reads from the socket
// - thread safety - this thread is the only one that writes to the socket
void spawn_cli_handler(tcp::socket & socket) {
  std::thread([&]() {
    while (true) {
      std::string cmd;
      std::getline(std::cin, cmd);
      asio::write(socket, asio::buffer(cmd));
    }
  }).detach();
}

// Reads data from the socket and prints it to stdout
awaitable<void> socket_task(asio::io_context & context, tcp::socket & socket) {
  try {
    while (true) {
      char data[2048];
      size_t n = co_await socket.async_read_some(asio::buffer(data), use_awaitable);
      std::string_view response(data, n);
      std::cout << "> " << response << std::endl;
    }
  } catch (std::exception & e) {
    std::cout << "Connection closed by server: " << e.what() << std::endl;
    // Stop the context to exit the program
    context.stop();
  }
}

int main(int argc, char * argv[]) {
  if (argc != 2) {
    std::cout << "Usage: client <addr:port>" << std::endl;
    std::cout << "Example: client localhost:3000" << std::endl;
    return 1;
  }

  auto const hostname_port = parse_hostname_port(argv[1]);
  if (!hostname_port) {
    std::cout << "Invalid server address: " << argv[1] << ". Expected format is <addr:port>" << std::endl;
    return 1;
  }

  try {
    asio::io_context io_context;

    // Graceful shutdown
    asio::signal_set signals(io_context, SIGINT, SIGTERM);
    signals.async_wait([&](auto, auto) {
      std::cout << "\nDisconnecting..." << std::endl;
      io_context.stop();
    });

    tcp::resolver resolver(io_context);
    tcp::resolver::query query(hostname_port->first, hostname_port->second);
    tcp::resolver::iterator endpoint_iterator = resolver.resolve(query);

    tcp::socket socket(io_context);
    asio::connect(socket, endpoint_iterator);

    spawn_cli_handler(socket);
    co_spawn(io_context, socket_task(io_context, socket), detached);

    io_context.run();
  } catch (std::exception & e) {
    std::cout << "Exception: " << e.what() << std::endl;
    return 1;
  }

  return 0;
}
