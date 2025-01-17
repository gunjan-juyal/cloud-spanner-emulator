//
// Copyright 2020 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#include <cstdint>
#include <limits>
#include <string>

#include "google/spanner/admin/database/v1/common.pb.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "zetasql/base/testing/status_matchers.h"
#include "tests/common/proto_matchers.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/time/civil_time.h"
#include "absl/time/time.h"
#include "google/cloud/spanner/json.h"
#include "tests/common/scoped_feature_flags_setter.h"
#include "tests/conformance/common/database_test_base.h"

namespace google {
namespace spanner {
namespace emulator {
namespace test {

namespace {

using testing::HasSubstr;
using zetasql_base::testing::StatusIs;
using JsonB = cloud::spanner::JsonB;

class PGFunctionsTest : public DatabaseTest {
 public:
  PGFunctionsTest() : feature_flags_({.enable_postgresql_interface = true}) {}

  void SetUp() override {
    dialect_ = database_api::DatabaseDialect::POSTGRESQL;
    DatabaseTest::SetUp();
  }

  absl::Status SetUpDatabase() override {
    return SetSchemaFromFile("pg_functions.test");
  }

 protected:
  void PopulateDatabase() {
    ZETASQL_EXPECT_OK(MultiInsert("values", {"id", "int_value", "double_value"},
                          {{1, 1, 2.1},
                           {2, 0, 3.2},
                           {3, 5, 1.2},
                           {4, Null<int64_t>(), Null<double>()}}));
  }

 private:
  test::ScopedEmulatorFeatureFlagsSetter feature_flags_;
};

TEST_F(PGFunctionsTest, CastToDate) {
  EXPECT_THAT(Query(
                  R"sql(
          SELECT CAST(col1 AS date) AS date
          FROM (SELECT 'jan 1, 2000' AS col1) subquery)sql"),
              IsOkAndHoldsRows({Date(2000, 1, 1)}));
}

// TODO: Re-enable after Spangres updates the error message.
TEST_F(PGFunctionsTest, DISABLED_CastToDateUnsupportedDate) {
  EXPECT_THAT(Query(
                  R"sql(
          SELECT CAST(col1 AS date) AS date
          FROM (SELECT 'jan 1, 0000' AS col1) subquery)sql"),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("date/time field value out of range")));
}

TEST_F(PGFunctionsTest, CastToTimestamp) {
  absl::TimeZone time_zone;
  ASSERT_TRUE(absl::LoadTimeZone("America/Los_Angeles", &time_zone));
  EXPECT_THAT(
      Query(
          R"sql(
          SELECT CAST(col1 AS timestamptz) AS timestamptz
          FROM (SELECT
            '2000/1/1 01:02:03 America/Los_Angeles' AS col1) subquery)sql"),
      IsOkAndHoldsRows({MakeTimestamp(absl::ToChronoTime(absl::FromCivil(
          absl::CivilSecond(2000, 1, 1, 1, 2, 3), time_zone)))}));
}

// TODO: Re-enable after Spangres updates the error message.
TEST_F(PGFunctionsTest, DISABLED_CastToTimestampUnsupportedTimestamp) {
  EXPECT_THAT(Query(
                  R"sql(
          SELECT CAST(col1 AS timestamptz) AS timestamptz
          FROM (SELECT
            '0000/1/1 01:02:03 America/Los_Angeles' AS col1) subquery)sql"),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("date/time field value out of range")));
}

TEST_F(PGFunctionsTest, MapDoubleToInt) {
  EXPECT_THAT(Query("SELECT 'NaN'::float = 'NaN'::float"),
              IsOkAndHoldsRows({true}));
}

TEST_F(PGFunctionsTest, LeastInteger) {
  EXPECT_THAT(Query("SELECT LEAST(2, 5, NULL, 1)"), IsOkAndHoldsRows({{1}}));
}

TEST_F(PGFunctionsTest, GreatestInteger) {
  EXPECT_THAT(Query("SELECT GREATEST(3, 7, NULL, 2)"), IsOkAndHoldsRows({{7}}));
}

// TODO: turn on test once PG.NUMERIC is supported.
TEST_F(PGFunctionsTest, DISABLED_LeastDoubles) {
  EXPECT_THAT(Query("SELECT LEAST(2.1, 5.5, 'NaN'::float, NULL)"),
              IsOkAndHoldsRows({{2.1}}));
}

// TODO: turn on test once PG.NUMERIC is supported.
TEST_F(PGFunctionsTest, DISABLED_GreatestDoubles) {
  EXPECT_THAT(Query("SELECT GREATEST(2.1, 5.5, 'NaN'::float, NULL)"),
              IsOkAndHoldsRows({{std::numeric_limits<double>::quiet_NaN()}}));
}

// MIN for the double type uses a different aggregator function than for other
// types so we test for doubles and not doubles.
TEST_F(PGFunctionsTest, MinDoubles) {
  EXPECT_THAT(Query("SELECT MIN('NaN'::float)"),
              IsOkAndHoldsRows({{std::numeric_limits<double>::quiet_NaN()}}));
}

TEST_F(PGFunctionsTest, MinDoublesFromTable) {
  PopulateDatabase();
  EXPECT_THAT(Query("SELECT MIN(double_value) FROM values"),
              IsOkAndHoldsRows({{1.2}}));
}

TEST_F(PGFunctionsTest, MinNotDoubles) {
  EXPECT_THAT(Query("SELECT MIN(12345)"), IsOkAndHoldsRows({{12345}}));
}

TEST_F(PGFunctionsTest, MinNotDoublesFromTable) {
  EXPECT_THAT(Query("SELECT MIN(int_value) FROM values"),
              IsOkAndHoldsRows({{Null<int64_t>()}}));
}

TEST_F(PGFunctionsTest, ArrayUpper) {
  EXPECT_THAT(Query("SELECT array_upper(ARRAY[true, false, true, false], 1)"),
              IsOkAndHoldsRows({{4}}));
  EXPECT_THAT(
      Query("SELECT array_upper(ARRAY['bytes1'::bytea, 'bytes2'::bytea], 1)"),
      IsOkAndHoldsRows({{2}}));
  EXPECT_THAT(Query("SELECT array_upper(ARRAY['1970-01-01'::date, "
                    "'1969-01-01'::date, '1968-01-01'::date], 1)"),
              IsOkAndHoldsRows({{3}}));
  EXPECT_THAT(Query("SELECT array_upper(ARRAY[1::float8, 2::float8, 3::float8, "
                    "4::float8, 5::float8], 1)"),
              IsOkAndHoldsRows({{5}}));
  EXPECT_THAT(Query("SELECT array_upper(ARRAY[0,1,2,3,4,5,6,7,8,9], 1)"),
              IsOkAndHoldsRows({{10}}));
  EXPECT_THAT(
      Query("SELECT array_upper(ARRAY['test1'::text, 'test2'::text], 1)"),
      IsOkAndHoldsRows({{2}}));
  EXPECT_THAT(
      Query("SELECT array_upper(ARRAY['1970-01-01 02:03:04'::timestamptz], 1)"),
      IsOkAndHoldsRows({{1}}));

  // Returns null for empty array
  EXPECT_THAT(Query("SELECT array_upper(ARRAY[]::bigint[], 1)"),
              IsOkAndHoldsRows({{Null<int64_t>()}}));

  // Returns null for dimensions <= 0
  EXPECT_THAT(Query("SELECT array_upper(ARRAY[1,2,3], 0)"),
              IsOkAndHoldsRows({{Null<int64_t>()}}));
  EXPECT_THAT(Query("SELECT array_upper(ARRAY[1,2,3], -1)"),
              IsOkAndHoldsRows({{Null<int64_t>()}}));

  // Error cases
  EXPECT_THAT(
      Query("SELECT array_upper(ARRAY[1,2,3], 2)"),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr("multi-dimensional arrays are not supported")));
}

TEST_F(PGFunctionsTest, Textregexne) {
  EXPECT_THAT(Query("SELECT textregexne('abcdefg', 'bb.*')"),
              IsOkAndHoldsRows({{true}}));
  EXPECT_THAT(Query("SELECT 'abcdefg' !~ 'bb.*'"), IsOkAndHoldsRows({{true}}));
  EXPECT_THAT(Query("SELECT textregexne('abcdefg', 'bc.*')"),
              IsOkAndHoldsRows({{false}}));
  EXPECT_THAT(Query("SELECT 'abcdefg' !~ 'bc.*'"), IsOkAndHoldsRows({{false}}));

  // Error cases
  EXPECT_THAT(Query("SELECT textregexne('abcd', '(a.c')"),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("invalid regular expression")));
  EXPECT_THAT(Query(absl::StrCat("SELECT '", std::string(10000, 'a'), "' !~ '",
                                 std::string(20000, '('),
                                 std::string(20000, ')'), "'")),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("regular expression is too complex")));
}

TEST_F(PGFunctionsTest, DateMi) {
  EXPECT_THAT(Query("SELECT date_mi('2001-02-01', '2001-01-01')"),
              IsOkAndHoldsRows({{31}}));
  EXPECT_THAT(Query("SELECT '2001-02-01'::date - '2001-01-01'::date"),
              IsOkAndHoldsRows({{31}}));
  EXPECT_THAT(Query("SELECT date_mi('2001-01-01', '2001-01-01')"),
              IsOkAndHoldsRows({{0}}));
  EXPECT_THAT(Query("SELECT '2001-01-01'::date - '2001-01-01'::date"),
              IsOkAndHoldsRows({{0}}));
  EXPECT_THAT(Query("SELECT date_mi('2001-01-01', '2001-02-01')"),
              IsOkAndHoldsRows({{-31}}));
  EXPECT_THAT(Query("SELECT '2001-01-01'::date - '2001-02-01'::date"),
              IsOkAndHoldsRows({{-31}}));
}

TEST_F(PGFunctionsTest, DateMii) {
  EXPECT_THAT(Query("SELECT date_mii('2001-01-01', 365)"),
              IsOkAndHoldsRows({Date(2000, 1, 2)}));  // Leap year
  EXPECT_THAT(Query("SELECT '2001-01-01'::date - 365"),
              IsOkAndHoldsRows({Date(2000, 1, 2)}));

  // Error cases
  EXPECT_THAT(Query("SELECT '0001-01-01'::date - 1"),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Date is out of supported range")));
}

TEST_F(PGFunctionsTest, DatePli) {
  EXPECT_THAT(Query("SELECT date_pli('2001-01-01', 365)"),
              IsOkAndHoldsRows({Date(2002, 1, 1)}));
  EXPECT_THAT(Query("SELECT '2001-01-01'::date + 365"),
              IsOkAndHoldsRows({Date(2002, 1, 1)}));

  // Error cases
  EXPECT_THAT(Query("SELECT '9999-12-31'::date + 1"),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Date is out of supported range")));
}

TEST_F(PGFunctionsTest, ToDate) {
  EXPECT_THAT(Query("SELECT to_date('01 Jan 1970', 'DD Mon YYYY')"),
              IsOkAndHoldsRows({Date(1970, 1, 1)}));

  // Error cases
  EXPECT_THAT(Query("SELECT to_date('0000-02-01', 'YYYY-MM-DD')"),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Date is out of supported range")));
}

TEST_F(PGFunctionsTest, ToTimestamp) {
  EXPECT_THAT(
      Query("SELECT to_timestamp('01 Jan 1970 17 03 04 +00', 'DD "
            "Mon YYYY HH24 MI SS TZH')"),
      IsOkAndHoldsRows({MakeTimestamp(absl::ToChronoTime(absl::FromCivil(
          absl::CivilSecond(1970, 1, 1, 17, 3, 4), absl::UTCTimeZone())))}));

  // Error cases
  EXPECT_THAT(Query("SELECT to_timestamp('1997 BC 11 16', 'YYYY BC MM DD')"),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Timestamp is out of supported range")));
  EXPECT_THAT(
      Query("SELECT to_timestamp('2011-12-18 11:38 PST', 'YYYY-MM-DD HH12:MI "
            "TZ')"),
      StatusIs(
          absl::StatusCode::kUnimplemented,
          HasSubstr("formatting field \"TZ\" is only supported in to_char")));
}

TEST_F(PGFunctionsTest, ToChar) {
  EXPECT_THAT(Query("SELECT to_char(-123, '999PR')"),
              IsOkAndHoldsRows({{"<123>"}}));
  EXPECT_THAT(Query("SELECT to_char(-123::float8, '999.99PR')"),
              IsOkAndHoldsRows({{"<123.00>"}}));
  EXPECT_THAT(Query("SELECT to_char('1970-01-01 02:03:04'::timestamptz, "
                    "'YYYY-MM-DD HH24 MI SS')"),
              IsOkAndHoldsRows({{"1970-01-01 02 03 04"}}));

  // Error cases
  EXPECT_THAT(
      Query("SELECT to_char(9, '9.9V9')"),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr("cannot use \"V\" and decimal point together")));
  EXPECT_THAT(Query("SELECT to_char(-9, '9PR.9')"),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("\"9\" must be ahead of \"PR\"")));
}

TEST_F(PGFunctionsTest, QuoteIdent) {
  EXPECT_THAT(Query("SELECT quote_ident('test')"),
              IsOkAndHoldsRows({{"\"test\""}}));
}

TEST_F(PGFunctionsTest, Substring) {
  EXPECT_THAT(Query("SELECT substring('abcdefg', 'a(b.)')"),
              IsOkAndHoldsRows({{"bc"}}));

  // Error cases
  EXPECT_THAT(Query("SELECT substring('abcd', '(a.c')"),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("invalid regular expression")));
}

TEST_F(PGFunctionsTest, RegexpMatch) {
  EXPECT_THAT(Query("SELECT regexp_match('abcdefg', 'b.')"),
              IsOkAndHoldsRows({Array<std::string>({"bc"})}));
  EXPECT_THAT(Query("SELECT regexp_match('aBcdefg', 'b.', 'i')"),
              IsOkAndHoldsRows({Array<std::string>({"Bc"})}));

  // Error cases
  EXPECT_THAT(Query("SELECT regexp_match('abcd', '(a.c')"),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("invalid regular expression")));
}

TEST_F(PGFunctionsTest, RegexpSplitToArray) {
  EXPECT_THAT(Query("SELECT regexp_split_to_array('a1b2c3d', '[0-9]')"),
              IsOkAndHoldsRows({Array<std::string>({"a", "b", "c", "d"})}));
  EXPECT_THAT(Query("SELECT regexp_split_to_array('1A2b3C4', '[a-z]', 'i')"),
              IsOkAndHoldsRows({Array<std::string>({"1", "2", "3", "4"})}));

  // Error cases
  EXPECT_THAT(Query("SELECT regexp_split_to_array('abcd', '(a.c')"),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("invalid regular expression")));
}

TEST_F(PGFunctionsTest, DISABLED_JsonBSubscriptText) {
  EXPECT_THAT(
      Query("select jsonb_object_field_text('{\"a\":1}'::jsonb, 'a'::text)"),
      IsOkAndHoldsRows({{"1"}}));
  EXPECT_THAT(Query("select jsonb_array_element_text('[1,2]'::jsonb, 1)"),
              IsOkAndHoldsRows({"2"}));

  // Error cases
  // Less than 2 arguments
  EXPECT_THAT(
      Query("select jsonb_object_field_text('{\"a\":1}'::jsonb)"),
      StatusIs(absl::StatusCode::kNotFound, HasSubstr("does not exist")));
  // More than 2 arguments
  EXPECT_THAT(
      Query("select jsonb_array_element_text('[1,2]'::jsonb, 1, 2)"),
      StatusIs(absl::StatusCode::kNotFound, HasSubstr("does not exist")));
  // Invalid arguments
  EXPECT_THAT(
      Query("select jsonb_array_element_text(1, '[1,2]'::jsonb)"),
      StatusIs(absl::StatusCode::kNotFound, HasSubstr("does not exist")));
}

TEST_F(PGFunctionsTest, DISABLED_ToJsonB) {
  EXPECT_THAT(Query(R"(select to_jsonb(null::bigint))"),
              IsOkAndHoldsRows({Null<JsonB>()}));
  EXPECT_THAT(Query(R"(select to_jsonb(4))"), IsOkAndHoldsRows({JsonB("4")}));
  EXPECT_THAT(Query(R"(select to_jsonb(fAlSe))"),
              IsOkAndHoldsRows({JsonB("false")}));
  EXPECT_THAT(Query(R"(select to_jsonb(10419.85))"),
              IsOkAndHoldsRows({JsonB("10419.85")}));
  EXPECT_THAT(Query(R"(select to_jsonb('this is a string'::text))"),
              IsOkAndHoldsRows({JsonB("\"this is a string\"")}));
  EXPECT_THAT(Query(R"(select to_jsonb('hello'::bytea))"),
              IsOkAndHoldsRows({JsonB("\"\\\\x68656c6c6f\"")}));
  EXPECT_THAT(Query(R"(select to_jsonb('1999-01-08'::date))"),
              IsOkAndHoldsRows({JsonB("\"1999-01-08\"")}));
  EXPECT_THAT(Query(R"(select to_jsonb('1986-01-01T00:00:01Z'::timestamptz))"),
              IsOkAndHoldsRows({JsonB("\"1986-01-01T00:00:01+00:00\"")}));
  EXPECT_THAT(Query(R"(select to_jsonb('{" ", "ab"}'::bytea[]))"),
              IsOkAndHoldsRows({JsonB("[\"\\\\x20\", \"\\\\x6162\"]")}));
  EXPECT_THAT(Query(R"(select to_jsonb('{"b":[1e0],"a":[20e-1]}'::jsonb))"),
              IsOkAndHoldsRows({JsonB(R"({"a": [2.0], "b": [1]})")}));
  EXPECT_THAT(
      Query(R"(select to_jsonb('-15e1500'::numeric))"),
      IsOkAndHoldsRows({JsonB(std::string("-15" + std::string(1500, '0')))}));

  // Error cases
  // Less than 1 argument
  EXPECT_THAT(
      Query(R"(select to_jsonb())"),
      StatusIs(absl::StatusCode::kNotFound, HasSubstr("does not exist")));
  // More than 1 argument
  EXPECT_THAT(
      Query(R"(select to_jsonb(1, 2))"),
      StatusIs(absl::StatusCode::kNotFound, HasSubstr("does not exist")));
}

}  // namespace

}  // namespace test
}  // namespace emulator
}  // namespace spanner
}  // namespace google
