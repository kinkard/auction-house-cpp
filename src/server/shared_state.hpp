#pragma once

#include "storage.hpp"
#include "transaction_log.hpp"

#include <memory>

struct SharedState {
  Storage storage;
  TransactionLog transaction_log;
};
