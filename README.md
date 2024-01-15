# The Auction House

This is a code test project for [Senior Online Programmer (New Game IP)](https://www.ubisoft.com/en-us/company/careers/search/743999932289413-senior-online-programmer-new-game-ip-) position at Ubisoft Stockholm.

## Supported functionality

- User can login using `client` or telnet, once `server` is launched
- User can deposit or withdraw items, using the following command: `deposit/withdraw <item name> [quantity]`. For example, `deposit funds 100`
- User can see own items via `view_items`
- User can create immediate or auction sell orders using `sell [immediate|auction] <item_name> [<quantity>] <price>` command. For example, `sell Sword 1 100` will create a immediate sell order for 1 Sword for 100 funds. 5% + 1 funds will be taken as a fee
- User can see all sell orders via `view_sell_orders`
- User can buy item that is on sale or make a bid on auction order. Sell orders are refered by id. For example, `buy 20` will buy order #20, while `buy 20 200` will made a bid to the order #20 with 200 funds. User will see errors if order is not matched, if bid is smaller than current price and so on
- User will see notifications (if they are still connected) once their sell order is executed, either immediate or auction
- All transactions are available in transaction log

Technical details:

- State is managed by sqlite3 via transactions, that guarantee that server will never go in incorrect state
- Each user processed in a asynchronous manner (povered by boost.asio, that is included into the project as a standalone library), effectively utilizing CPU and memory
- Supported platforms: MacOS, Linux (tested on Ubuntu 22.04 LTS), Windows (VS2019)

## Build & Run

```sh
mkdir build && cd build
cmake .. && cmake --build . -j 10
./server 3000 db.sqlite transaction.log
```

Transaction log can be monitored via `tail -f transaction.log`.

## Client

This repo also contains a minimalistic client that sends everything you type in console to the server and prints everything server sends back. The telnet can be used instead.

```sh
$ ./client localhost:3000
> Welcome to Sundris Auction House, stranger! How can I call you?
Stepan
> Successfully logged in as Stepan
help
> Available commands:
- sell - Places an item for sale at a specified price. Format: 'sell [immediate|auction] <item_name> [<quantity>] <price>'
- view_sell_orders - Displays a list of all current sell orders from all users
- buy - Executes immediate sell order or places a bid on a auction sell order. Format: 'buy <sell_order_id> [<bid>]'
- view_items - Displays a list items for the current user
- whoami - Displays the username of the current user
- withdraw - Withdraws a specified amount from the user's account. Format: 'withdraw <item name> [<quantity>]'
- deposit - Deposits a specified amount into the user's account. Format: 'deposit <item name> [<quantity>]'
- help - Prints this help message about all available commands
- ping - Replies 'pong'
Usage: <command> [<args>], where `[]` annotates optional argumet(s)

```

## Dependencies

There are 5 external library used in this project

- [asio](deps/asio/) - for asynchronous runtime and network
- [sqlite3](deps/sqlite3/) - for state/storage. I've added my own C++ wrapper around it (see sqlite3.hpp/cpp) to reduce amount of boiler plate code. All queries live in storage.cpp and covered by tests in tests/storage_tests.cpp
- [fmt::fmt](deps/fmt/) - for nice and shiny formatting that would work with VS2019
- [tl::expected](deps/expected/) - C++11 compatible way to handle errors without throwing exceptions everywhere
- [gtest](deps/goodletest/) - for core logic tests

## VS2019 note

This Auction House implementation uses boost.asio (as a standalone library) and corutines, which are [supported by VS2019 from 16.8 version](https://learn.microsoft.com/en-us/cpp/overview/visual-cpp-language-conformance?view=msvc-170). Please ensure that you use the latest available version of the VS2019.
