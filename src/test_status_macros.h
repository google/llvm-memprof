/*
 * Copyright 2025 Google LLC
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef TEST_STATUS_MACROS_H_
#define TEST_STATUS_MACROS_H_

#include "absl/status/status.h"
#include "absl/status/statusor.h"

#define CONCAT_IMPL(x, y) x##y
#define CONCAT(x, y) CONCAT_IMPL(x, y)
#define ASSERT_OK_AND_ASSIGN(lhs, rexpr) \
  ASSERT_OK_AND_ASSIGN_IMPL(lhs, rexpr, __LINE__)
#define ASSERT_OK_AND_ASSIGN_IMPL(lhs, rexpr, counter)   \
  auto CONCAT(status_or_value_, counter) = (rexpr);      \
  ASSERT_OK(CONCAT(status_or_value_, counter).status()); \
  lhs = *std::move(CONCAT(status_or_value_, counter));

#define EXPECT_OK(statement) EXPECT_TRUE((statement).ok())
#define EXPECT_NOT_OK(statement) EXPECT_FALSE((statement).ok())
#define ASSERT_OK(statement) ASSERT_TRUE((statement).ok())
#define ASSERT_NOT_OK(statement) ASSERT_FALSE((statement).ok())

#endif  // TEST_STATUS_MACROS_H_
