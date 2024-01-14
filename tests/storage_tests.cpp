#include "gmock/gmock.h"
#include "storage.hpp"

#include <fmt/format.h>
#include <gmock/gmock-matchers.h>
#include <gtest/gtest.h>

#include <memory>
#include <ostream>

uint64_t constexpr expiration_time = 1609459200;
std::string_view constexpr expiration_time_str = "2021-01-01 00:00";

class StorageTest : public ::testing::Test {
protected:
  void SetUp() override {
    std::remove("test.db");
    auto storage = Storage::open("test.db");
    ASSERT_TRUE(storage) << storage.error();
    this->storage = std::make_unique<Storage>(std::move(storage.value()));
  }

  void TearDown() override { std::remove("test.db"); }

  std::unique_ptr<Storage> storage;
};

TEST_F(StorageTest, get_or_create_user) {
  ASSERT_EQ(storage->get_or_create_user("user1")->id, 1);
  ASSERT_EQ(storage->get_or_create_user("user2")->id, 2);
  ASSERT_EQ(storage->get_or_create_user("user3")->id, 3);

  ASSERT_EQ(storage->get_or_create_user("user1")->id, 1);
  ASSERT_EQ(storage->get_or_create_user("user2")->id, 2);
  ASSERT_EQ(storage->get_or_create_user("user3")->id, 3);
}

TEST_F(StorageTest, funds) {
  auto user = *storage->get_or_create_user("user1");
  // freshly created user always has 0 funds
  EXPECT_THAT(*storage->view_items(user.id), testing::ElementsAre(std::make_pair("funds", 0)));

  ASSERT_TRUE(storage->deposit(user.id, "funds", 10));
  EXPECT_THAT(*storage->view_items(user.id), testing::ElementsAre(std::make_pair("funds", 10)));

  ASSERT_TRUE(storage->withdraw(user.id, "funds", 7));
  EXPECT_THAT(*storage->view_items(user.id), testing::ElementsAre(std::make_pair("funds", 3)));

  ASSERT_TRUE(storage->withdraw(user.id, "funds", 3));
  EXPECT_THAT(*storage->view_items(user.id), testing::ElementsAre(std::make_pair("funds", 0)));

  ASSERT_TRUE(storage->deposit(user.id, "funds", 5));

  // get_or_create_user should not create new funds
  user = *storage->get_or_create_user("user1");
  EXPECT_THAT(*storage->view_items(user.id), testing::ElementsAre(std::make_pair("funds", 5)));

  // withdraw more than we have
  ASSERT_FALSE(storage->withdraw(user.id, "funds", 10));

  // deposit negative amount
  ASSERT_FALSE(storage->deposit(user.id, "funds", -10));

  // withdraw negative amount
  ASSERT_FALSE(storage->withdraw(user.id, "funds", -10));

  // deposit to non-existing user
  ASSERT_FALSE(storage->deposit(100, "funds", 10));

  // withdraw from non-existing user
  ASSERT_FALSE(storage->withdraw(100, "funds", 10));

  // and finally check that we can deposit and withdraw from different users
  user = *storage->get_or_create_user("user2");
  ASSERT_TRUE(storage->deposit(user.id, "funds", 20));

  user = *storage->get_or_create_user("user3");
  ASSERT_TRUE(storage->deposit(user.id, "funds", 30));

  // user1 is still untouched
  user = *storage->get_or_create_user("user1");
  EXPECT_THAT(*storage->view_items(user.id), testing::ElementsAre(std::make_pair("funds", 5)));

  // user2 has 20 funds
  user = *storage->get_or_create_user("user2");
  EXPECT_THAT(*storage->view_items(user.id), testing::ElementsAre(std::make_pair("funds", 20)));

  // user3 has 30 funds
  user = *storage->get_or_create_user("user3");
  EXPECT_THAT(*storage->view_items(user.id), testing::ElementsAre(std::make_pair("funds", 30)));

  user = *storage->get_or_create_user("foo");
  EXPECT_THAT(*storage->view_items(user.id), testing::ElementsAre(std::make_pair("funds", 0)));
  ASSERT_TRUE(storage->deposit(user.id, "funds", 100500));
  ASSERT_TRUE(storage->withdraw(user.id, "funds", 100400));
}

TEST_F(StorageTest, items) {
  auto user = *storage->get_or_create_user("user1");
  // freshly created user always has 0 items
  EXPECT_THAT(*storage->view_items(user.id), testing::ElementsAre(std::make_pair("funds", 0)));

  ASSERT_TRUE(storage->deposit(user.id, "item1", 10));
  EXPECT_THAT(*storage->view_items(user.id),
              testing::ElementsAre(std::make_pair("funds", 0), std::make_pair("item1", 10)));

  ASSERT_TRUE(storage->deposit(user.id, "item2", 20));
  EXPECT_THAT(
      *storage->view_items(user.id),
      testing::ElementsAre(std::make_pair("funds", 0), std::make_pair("item1", 10), std::make_pair("item2", 20)));

  ASSERT_TRUE(storage->withdraw(user.id, "item1", 5));
  EXPECT_THAT(
      *storage->view_items(user.id),
      testing::ElementsAre(std::make_pair("funds", 0), std::make_pair("item1", 5), std::make_pair("item2", 20)));

  ASSERT_TRUE(storage->withdraw(user.id, "item2", 10));
  EXPECT_THAT(
      *storage->view_items(user.id),
      testing::ElementsAre(std::make_pair("funds", 0), std::make_pair("item1", 5), std::make_pair("item2", 10)));

  // get_or_create_user should not create new items
  user = *storage->get_or_create_user("user1");
  EXPECT_THAT(
      *storage->view_items(user.id),
      testing::ElementsAre(std::make_pair("funds", 0), std::make_pair("item1", 5), std::make_pair("item2", 10)));

  // withdraw more than we have
  ASSERT_FALSE(storage->withdraw(user.id, "item1", 10));

  // deposit negative amount
  ASSERT_FALSE(storage->deposit(user.id, "item1", -10));

  // withdraw negative amount
  ASSERT_FALSE(storage->withdraw(user.id, "item1", -10));

  // deposit to non-existing user
  ASSERT_FALSE(storage->deposit(100, "item1", 10));

  // withdraw from non-existing user
  ASSERT_FALSE(storage->withdraw(100, "item1", 10));
}

bool operator==(const SellOrder & lhs, const SellOrder & rhs) {
  return lhs.id == rhs.id && lhs.seller_name == rhs.seller_name && lhs.item_name == rhs.item_name &&
         lhs.quantity == rhs.quantity && lhs.price == rhs.price && lhs.expiration_time == rhs.expiration_time &&
         lhs.type == rhs.type;
}

std::ostream & operator<<(std::ostream & os, const SellOrder & order) {
  return os << fmt::format(
             "SellOrder{{.id={}, .user_name={}, .item_name={}, .quantity={}, .price={}, "
             ".expiration_time={}, .type={}}}",
             order.id, order.seller_name, order.item_name, order.quantity, order.price, order.expiration_time,
             to_string(order.type));
}

class GeneralSellOrderTest : public StorageTest, public ::testing::WithParamInterface<SellOrderType> {};

TEST_P(GeneralSellOrderTest, negative) {
  auto const order_type = GetParam();

  auto user = *storage->get_or_create_user("user");
  ASSERT_TRUE(storage->deposit(user.id, "funds", 100));
  ASSERT_TRUE(storage->deposit(user.id, "item1", 10));
  ASSERT_TRUE(storage->deposit(user.id, "item2", 20));
  EXPECT_THAT(
      *storage->view_items(user.id),
      testing::ElementsAre(std::make_pair("funds", 100), std::make_pair("item1", 10), std::make_pair("item2", 20)));
  EXPECT_THAT(*storage->view_sell_orders(), testing::IsEmpty());

  // try to sell more than we have
  ASSERT_FALSE(storage->place_sell_order(order_type, user.id, "item1", 110, 10, expiration_time));

  // try to sell negative amount
  ASSERT_FALSE(storage->place_sell_order(order_type, user.id, "item1", -10, 10, expiration_time));

  // try to sell for negative price
  ASSERT_FALSE(storage->place_sell_order(order_type, user.id, "item1", 10, -10, expiration_time));

  // try to sell non-existing item
  ASSERT_FALSE(storage->place_sell_order(order_type, user.id, "non existing item", 10, 10, expiration_time));

  // try to sell to from non-existing user
  ASSERT_FALSE(storage->place_sell_order(order_type, 100, "item1", 10, 10, expiration_time));

  // cannot sell funds
  ASSERT_FALSE(storage->place_sell_order(order_type, user.id, "funds", 10, 10, expiration_time));

  // Finally, nothing should be changed
  EXPECT_THAT(*storage->view_sell_orders(), testing::IsEmpty());
}

TEST_P(GeneralSellOrderTest, auction_house_fee) {
  auto const order_type = GetParam();

  auto user = *storage->get_or_create_user("user");
  ASSERT_TRUE(storage->deposit(user.id, "item1", 10));
  ASSERT_TRUE(storage->deposit(user.id, "item2", 20));
  EXPECT_THAT(
      *storage->view_items(user.id),
      testing::ElementsAre(std::make_pair("funds", 0), std::make_pair("item1", 10), std::make_pair("item2", 20)));

  // Not enough funds to pay auction house fee
  ASSERT_FALSE(storage->place_sell_order(order_type, user.id, "item1", 10, 200, expiration_time));
  // nothing changed
  EXPECT_THAT(
      *storage->view_items(user.id),
      testing::ElementsAre(std::make_pair("funds", 0), std::make_pair("item1", 10), std::make_pair("item2", 20)));
  EXPECT_THAT(*storage->view_sell_orders(), testing::IsEmpty());

  // Now with enough funds
  ASSERT_TRUE(storage->deposit(user.id, "funds", 100));

  int const price = 200;
  int const fee = price / 20 + 1;  // 5% + 1

  ASSERT_TRUE(storage->place_sell_order(order_type, user.id, "item1", 10, price, expiration_time));
  EXPECT_THAT(*storage->view_items(user.id),
              testing::ElementsAre(std::make_pair("funds", 100 - fee), std::make_pair("item2", 20)));

  // cancel expired orders
  ASSERT_TRUE(storage->process_expired_sell_orders(expiration_time));

  // items are returned but fee is not
  EXPECT_THAT(*storage->view_items(user.id),
              testing::ElementsAre(std::make_pair("funds", 100 - fee), std::make_pair("item1", 10),
                                   std::make_pair("item2", 20)));
}

TEST_P(GeneralSellOrderTest, positive) {
  auto const order_type = GetParam();

  auto user = *storage->get_or_create_user("user");
  ASSERT_TRUE(storage->deposit(user.id, "funds", 100));
  ASSERT_TRUE(storage->deposit(user.id, "item1", 10));
  ASSERT_TRUE(storage->deposit(user.id, "item2", 20));
  EXPECT_THAT(
      *storage->view_items(user.id),
      testing::ElementsAre(std::make_pair("funds", 100), std::make_pair("item1", 10), std::make_pair("item2", 20)));

  for (int i = 1; i < 10; ++i) {
    ASSERT_TRUE(storage->place_sell_order(order_type, user.id, "item1", 1, 10 + i, expiration_time));
    EXPECT_THAT(*storage->view_items(user.id),
                testing::ElementsAre(std::make_pair("funds", 100 - i /* fee */), std::make_pair("item1", 10 - i),
                                     std::make_pair("item2", 20)));
  }

  ASSERT_TRUE(storage->place_sell_order(order_type, user.id, "item2", 15, 100, expiration_time));
  EXPECT_THAT(
      *storage->view_items(user.id),
      testing::ElementsAre(std::make_pair("funds", 85), std::make_pair("item1", 1), std::make_pair("item2", 5)));

  ASSERT_TRUE(storage->place_sell_order(order_type, user.id, "item2", 5, 100, expiration_time + 1));
  EXPECT_THAT(*storage->view_items(user.id),
              testing::ElementsAre(std::make_pair("funds", 79), std::make_pair("item1", 1)));

  EXPECT_THAT(*storage->view_sell_orders(), testing::ElementsAre(
                                                SellOrder{
                                                    .id = 1,
                                                    .seller_name = "user",
                                                    .item_name = "item1",
                                                    .quantity = 1,
                                                    .price = 11,
                                                    .expiration_time = "2021-01-01 00:00:00",
                                                    .type = order_type,
                                                },
                                                SellOrder{
                                                    .id = 2,
                                                    .seller_name = "user",
                                                    .item_name = "item1",
                                                    .quantity = 1,
                                                    .price = 12,
                                                    .expiration_time = "2021-01-01 00:00:00",
                                                    .type = order_type,
                                                },
                                                SellOrder{
                                                    .id = 3,
                                                    .seller_name = "user",
                                                    .item_name = "item1",
                                                    .quantity = 1,
                                                    .price = 13,
                                                    .expiration_time = "2021-01-01 00:00:00",
                                                    .type = order_type,
                                                },
                                                SellOrder{
                                                    .id = 4,
                                                    .seller_name = "user",
                                                    .item_name = "item1",
                                                    .quantity = 1,
                                                    .price = 14,
                                                    .expiration_time = "2021-01-01 00:00:00",
                                                    .type = order_type,
                                                },
                                                SellOrder{
                                                    .id = 5,
                                                    .seller_name = "user",
                                                    .item_name = "item1",
                                                    .quantity = 1,
                                                    .price = 15,
                                                    .expiration_time = "2021-01-01 00:00:00",
                                                    .type = order_type,
                                                },
                                                SellOrder{
                                                    .id = 6,
                                                    .seller_name = "user",
                                                    .item_name = "item1",
                                                    .quantity = 1,
                                                    .price = 16,
                                                    .expiration_time = "2021-01-01 00:00:00",
                                                    .type = order_type,
                                                },
                                                SellOrder{
                                                    .id = 7,
                                                    .seller_name = "user",
                                                    .item_name = "item1",
                                                    .quantity = 1,
                                                    .price = 17,
                                                    .expiration_time = "2021-01-01 00:00:00",
                                                    .type = order_type,
                                                },
                                                SellOrder{
                                                    .id = 8,
                                                    .seller_name = "user",
                                                    .item_name = "item1",
                                                    .quantity = 1,
                                                    .price = 18,
                                                    .expiration_time = "2021-01-01 00:00:00",
                                                    .type = order_type,
                                                },
                                                SellOrder{
                                                    .id = 9,
                                                    .seller_name = "user",
                                                    .item_name = "item1",
                                                    .quantity = 1,
                                                    .price = 19,
                                                    .expiration_time = "2021-01-01 00:00:00",
                                                    .type = order_type,
                                                },
                                                SellOrder{
                                                    .id = 10,
                                                    .seller_name = "user",
                                                    .item_name = "item2",
                                                    .quantity = 15,
                                                    .price = 100,
                                                    .expiration_time = "2021-01-01 00:00:00",
                                                    .type = order_type,
                                                },
                                                SellOrder{
                                                    .id = 11,
                                                    .seller_name = "user",
                                                    .item_name = "item2",
                                                    .quantity = 5,
                                                    .price = 100,
                                                    .expiration_time = "2021-01-01 00:00:01",
                                                    .type = order_type,
                                                }));

  // cancel expired orders
  auto cancel_result = storage->process_expired_sell_orders(expiration_time);
  ASSERT_TRUE(cancel_result) << cancel_result.error();
  EXPECT_THAT(*storage->view_sell_orders(), testing::ElementsAre(SellOrder{
                                                .id = 11,
                                                .seller_name = "user",
                                                .item_name = "item2",
                                                .quantity = 5,
                                                .price = 100,
                                                .expiration_time = "2021-01-01 00:00:01",
                                                .type = order_type,
                                            }));
  // items are returned but fee is not
  EXPECT_THAT(
      *storage->view_items(user.id),
      testing::ElementsAre(std::make_pair("funds", 79), std::make_pair("item1", 10), std::make_pair("item2", 15)));

  // And finally, cancel the last order
  ASSERT_TRUE(storage->process_expired_sell_orders(expiration_time + 2));
  EXPECT_THAT(
      *storage->view_items(user.id),
      testing::ElementsAre(std::make_pair("funds", 79), std::make_pair("item1", 10), std::make_pair("item2", 20)));
  EXPECT_THAT(*storage->view_sell_orders(), testing::IsEmpty());
}

INSTANTIATE_TEST_SUITE_P(GeneralSellOrderTest, GeneralSellOrderTest,
                         ::testing::Values(SellOrderType::Immediate, SellOrderType::Auction));

TEST_F(StorageTest, execute_immediate_sell_order_error) {
  auto seller = *storage->get_or_create_user("seller");
  ASSERT_TRUE(storage->deposit(seller.id, "funds", 100));
  ASSERT_TRUE(storage->deposit(seller.id, "item1", 10));
  ASSERT_TRUE(storage->place_sell_order(SellOrderType::Immediate, seller.id, "item1", 7, 10, expiration_time));
  ASSERT_TRUE(storage->place_sell_order(SellOrderType::Auction, seller.id, "item1", 3, 11, expiration_time));
  EXPECT_THAT(*storage->view_sell_orders(), testing::ElementsAre(
                                                SellOrder{
                                                    .id = 1,
                                                    .seller_name = "seller",
                                                    .item_name = "item1",
                                                    .quantity = 7,
                                                    .price = 10,
                                                    .expiration_time = "2021-01-01 00:00:00",
                                                    .type = SellOrderType::Immediate,
                                                },
                                                SellOrder{
                                                    .id = 2,
                                                    .seller_name = "seller",
                                                    .item_name = "item1",
                                                    .quantity = 3,
                                                    .price = 11,
                                                    .expiration_time = "2021-01-01 00:00:00",
                                                    .type = SellOrderType::Auction,
                                                }));

  // You can't buy your own items
  ASSERT_FALSE(storage->execute_immediate_sell_order(seller.id, 1));

  auto buyer = *storage->get_or_create_user("buyer");

  // try to buy non-existing sell order
  ASSERT_FALSE(storage->execute_immediate_sell_order(buyer.id, 100));

  // try to buy from non-existing user
  ASSERT_FALSE(storage->execute_immediate_sell_order(100, 1));

  // try to buy without enough funds
  ASSERT_FALSE(storage->execute_immediate_sell_order(buyer.id, 1));

  // try to buy auction order with not enough funds
  ASSERT_FALSE(storage->execute_immediate_sell_order(buyer.id, 2));

  // repeat with funds
  ASSERT_TRUE(storage->deposit(buyer.id, "funds", 100));

  // still can't buy auction order
  ASSERT_FALSE(storage->execute_immediate_sell_order(buyer.id, 2));

  // while immediate order should be bought
  ASSERT_TRUE(storage->execute_immediate_sell_order(buyer.id, 1));
}

TEST_F(StorageTest, place_bid_on_auction_sell_order) {
  auto seller = *storage->get_or_create_user("seller");
  ASSERT_TRUE(storage->deposit(seller.id, "funds", 100));
  ASSERT_TRUE(storage->deposit(seller.id, "item1", 10));
  ASSERT_TRUE(storage->place_sell_order(SellOrderType::Immediate, seller.id, "item1", 7, 10, expiration_time));
  ASSERT_TRUE(storage->place_sell_order(SellOrderType::Auction, seller.id, "item1", 3, 11, expiration_time));
  EXPECT_THAT(*storage->view_sell_orders(), testing::ElementsAre(
                                                SellOrder{
                                                    .id = 1,
                                                    .seller_name = "seller",
                                                    .item_name = "item1",
                                                    .quantity = 7,
                                                    .price = 10,
                                                    .expiration_time = "2021-01-01 00:00:00",
                                                    .type = SellOrderType::Immediate,
                                                },
                                                SellOrder{
                                                    .id = 2,
                                                    .seller_name = "seller",
                                                    .item_name = "item1",
                                                    .quantity = 3,
                                                    .price = 11,
                                                    .expiration_time = "2021-01-01 00:00:00",
                                                    .type = SellOrderType::Auction,
                                                }));

  // You can't can't place a bid on your own items
  ASSERT_FALSE(storage->place_bid_on_auction_sell_order(seller.id, 2, 20));

  auto buyer = *storage->get_or_create_user("buyer");

  // try to can't place a bid on non-existing sell order
  ASSERT_FALSE(storage->place_bid_on_auction_sell_order(buyer.id, 100, 20));

  // try to can't place a bid from non-existing user
  ASSERT_FALSE(storage->place_bid_on_auction_sell_order(100, 2, 20));

  // try to can't place a bid without enough funds
  ASSERT_FALSE(storage->place_bid_on_auction_sell_order(buyer.id, 20, 20));

  // try to can't place a bid on auction order with not enough funds
  ASSERT_FALSE(storage->place_bid_on_auction_sell_order(buyer.id, 1, 20));

  // repeat with funds
  ASSERT_TRUE(storage->deposit(buyer.id, "funds", 100));

  // still can't place a bid on immediate order
  ASSERT_FALSE(storage->place_bid_on_auction_sell_order(buyer.id, 1, 20));

  // while it is possible to place a bid on auction order
  ASSERT_TRUE(storage->place_bid_on_auction_sell_order(buyer.id, 2, 20));
  EXPECT_THAT(*storage->view_items(buyer.id), testing::ElementsAre(std::make_pair("funds", 80)));

  // check that bid is placed
  EXPECT_THAT(*storage->view_sell_orders(), testing::ElementsAre(
                                                SellOrder{
                                                    .id = 1,
                                                    .seller_name = "seller",
                                                    .item_name = "item1",
                                                    .quantity = 7,
                                                    .price = 10,
                                                    .expiration_time = "2021-01-01 00:00:00",
                                                    .type = SellOrderType::Immediate,
                                                },
                                                SellOrder{
                                                    .id = 2,
                                                    .seller_name = "seller",
                                                    .item_name = "item1",
                                                    .quantity = 3,
                                                    .price = 20,  // a bid was made!
                                                    .expiration_time = "2021-01-01 00:00:00",
                                                    .type = SellOrderType::Auction,
                                                }));

  // but you can't repeat a bid
  ASSERT_FALSE(storage->place_bid_on_auction_sell_order(buyer.id, 2, 20));

  auto another_buyer = *storage->get_or_create_user("another buyer");
  ASSERT_TRUE(storage->deposit(another_buyer.id, "funds", 100));

  // and you can't lower previous bid
  ASSERT_FALSE(storage->place_bid_on_auction_sell_order(another_buyer.id, 2, 19));

  // but you can increase it, but not greater than funds allow
  ASSERT_FALSE(storage->place_bid_on_auction_sell_order(another_buyer.id, 2, 121));

  ASSERT_TRUE(storage->place_bid_on_auction_sell_order(another_buyer.id, 2, 21));

  EXPECT_THAT(*storage->view_items(seller.id), testing::ElementsAre(std::make_pair("funds", 98)));
  EXPECT_THAT(*storage->view_items(buyer.id), testing::ElementsAre(std::make_pair("funds", 100)));
  EXPECT_THAT(*storage->view_items(another_buyer.id), testing::ElementsAre(std::make_pair("funds", 79)));

  // and finally process expired orders
  ASSERT_TRUE(storage->process_expired_sell_orders(expiration_time));
  // seller receives funds from the buyer and items from the immediate order
  EXPECT_THAT(*storage->view_items(seller.id),
              testing::ElementsAre(std::make_pair("funds", 98 + 21), std::make_pair("item1", 7)));
  // buyer receives nothing, his bid was outbid
  EXPECT_THAT(*storage->view_items(buyer.id), testing::ElementsAre(std::make_pair("funds", 100)));
  // another buyer receives items from the auction order
  EXPECT_THAT(*storage->view_items(another_buyer.id),
              testing::ElementsAre(std::make_pair("funds", 79), std::make_pair("item1", 3)));
}

TEST_F(StorageTest, execute_immediate_sell_order_ok) {
  auto seller = *storage->get_or_create_user("user");
  ASSERT_TRUE(storage->deposit(seller.id, "funds", 100));
  ASSERT_TRUE(storage->deposit(seller.id, "item1", 10));
  ASSERT_TRUE(storage->deposit(seller.id, "item2", 20));
  EXPECT_THAT(
      *storage->view_items(seller.id),
      testing::ElementsAre(std::make_pair("funds", 100), std::make_pair("item1", 10), std::make_pair("item2", 20)));

  ASSERT_TRUE(storage->place_sell_order(SellOrderType::Immediate, seller.id, "item1", 2, 2, expiration_time));

  // sell fee is (5% + 1)
  EXPECT_THAT(
      *storage->view_items(seller.id),
      testing::ElementsAre(std::make_pair("funds", 99), std::make_pair("item1", 8), std::make_pair("item2", 20)));

  ASSERT_TRUE(storage->place_sell_order(SellOrderType::Immediate, seller.id, "item1", 3, 3, expiration_time));
  ASSERT_TRUE(storage->place_sell_order(SellOrderType::Immediate, seller.id, "item1", 4, 4, expiration_time));
  ASSERT_TRUE(storage->place_sell_order(SellOrderType::Immediate, seller.id, "item1", 1, 4, expiration_time));

  ASSERT_TRUE(storage->place_sell_order(SellOrderType::Immediate, seller.id, "item2", 5, 5, expiration_time));
  ASSERT_TRUE(storage->place_sell_order(SellOrderType::Immediate, seller.id, "item2", 10, 10, expiration_time));
  ASSERT_TRUE(storage->place_sell_order(SellOrderType::Immediate, seller.id, "item2", 5, 15, expiration_time));

  EXPECT_THAT(*storage->view_items(seller.id), testing::ElementsAre(std::make_pair("funds", 93)));

  auto buyer = *storage->get_or_create_user("buyer");
  ASSERT_TRUE(storage->deposit(buyer.id, "funds", 20));

  // 1 item1 for 4 funds
  ASSERT_TRUE(storage->execute_immediate_sell_order(buyer.id, 4));

  // check items and funds
  EXPECT_THAT(*storage->view_items(buyer.id),
              testing::ElementsAre(std::make_pair("funds", 16), std::make_pair("item1", 1)));
  EXPECT_THAT(*storage->view_items(seller.id), testing::ElementsAre(std::make_pair("funds", 97)));
}
