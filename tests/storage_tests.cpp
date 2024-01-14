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
                testing::ElementsAre(std::make_pair("funds", 100), std::make_pair("item1", 10 - i),
                                     std::make_pair("item2", 20)));
  }

  ASSERT_TRUE(storage->place_sell_order(order_type, user.id, "item2", 15, 100, expiration_time));
  EXPECT_THAT(
      *storage->view_items(user.id),
      testing::ElementsAre(std::make_pair("funds", 100), std::make_pair("item1", 1), std::make_pair("item2", 5)));

  ASSERT_TRUE(storage->place_sell_order(order_type, user.id, "item2", 5, 100, expiration_time + 1));
  EXPECT_THAT(*storage->view_items(user.id),
              testing::ElementsAre(std::make_pair("funds", 100), std::make_pair("item1", 1)));

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
  auto cancel_result = storage->cancel_expired_sell_orders(expiration_time);
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
  EXPECT_THAT(
      *storage->view_items(user.id),
      testing::ElementsAre(std::make_pair("funds", 100), std::make_pair("item1", 10), std::make_pair("item2", 15)));

  // And finally
  ASSERT_TRUE(storage->cancel_expired_sell_orders(expiration_time + 2));
  EXPECT_THAT(
      *storage->view_items(user.id),
      testing::ElementsAre(std::make_pair("funds", 100), std::make_pair("item1", 10), std::make_pair("item2", 20)));
  EXPECT_THAT(*storage->view_sell_orders(), testing::IsEmpty());
}

INSTANTIATE_TEST_SUITE_P(GeneralSellOrderTest, GeneralSellOrderTest,
                         ::testing::Values(SellOrderType::Immediate, SellOrderType::Auction));
