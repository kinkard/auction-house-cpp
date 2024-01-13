#include <gtest/gtest.h>
#include <memory>

#include "storage.hpp"

TEST(Storage, smoke) {
  auto storage = Storage::open("test.db");
  ASSERT_TRUE(storage) << storage.error();
  auto deferred = std::shared_ptr<void>(nullptr, [](...) { std::remove("test.db"); });

  ASSERT_EQ(storage->get_or_create_user("user1")->id, 1);
  ASSERT_EQ(storage->get_or_create_user("user2")->id, 2);
  ASSERT_EQ(storage->get_or_create_user("user3")->id, 3);

  ASSERT_EQ(storage->get_or_create_user("user1")->id, 1);
  ASSERT_EQ(storage->get_or_create_user("user2")->id, 2);
  ASSERT_EQ(storage->get_or_create_user("user3")->id, 3);
}
