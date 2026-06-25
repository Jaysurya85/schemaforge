#include "schemaforge/generator/RealisticValueGenerator.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string>

#include "schemaforge/domain/ColumnDomainResolver.h"

namespace schemaforge {
namespace {

constexpr std::array<const char*, 20> first_names = {
    "Olivia", "Liam", "Emma", "Noah", "Ava", "Ethan", "Mia", "Lucas", "Sofia", "Mason",
    "Amelia", "James", "Harper", "Benjamin", "Evelyn", "Daniel", "Luna", "Henry", "Camila",
    "Alexander"};
constexpr std::array<const char*, 20> last_names = {
    "Smith", "Johnson", "Williams", "Brown", "Jones", "Garcia", "Miller", "Davis", "Wilson",
    "Anderson", "Thomas", "Taylor", "Moore", "Jackson", "Martin", "Lee", "Perez", "Thompson",
    "White", "Harris"};
constexpr std::array<const char*, 8> domains = {"example.com", "mail.test", "demo.org",
                                                "sample.net", "inbox.test", "example.org",
                                                "demo.test", "sample.org"};
constexpr std::array<const char*, 10> categories = {
    "Electronics", "Home", "Books", "Outdoors", "Clothing",
    "Beauty", "Grocery", "Office", "Sports", "Toys"};
constexpr std::array<const char*, 8> statuses = {
    "active", "pending", "completed", "processing", "shipped", "cancelled", "inactive", "draft"};
constexpr std::array<const char*, 10> product_adjectives = {
    "Compact", "Premium", "Everyday", "Classic", "Smart", "Portable", "Essential", "Modern",
    "Durable", "Lightweight"};
constexpr std::array<const char*, 10> product_nouns = {
    "Headphones", "Backpack", "Notebook", "Lamp", "Bottle",
    "Keyboard", "Jacket", "Speaker", "Organizer", "Camera"};
constexpr std::array<const char*, 8> title_adjectives = {
    "Practical", "Complete", "Modern", "Reliable", "Simple", "Essential", "Focused", "Clear"};
constexpr std::array<const char*, 8> companies = {
    "Northstar Labs", "Summit Works", "Brightline Studio", "Cedar Systems",
    "Harbor Analytics", "Atlas Supply", "Evergreen Digital", "Pioneer Group"};

struct AddressProfile {
  const char* city;
  const char* state;
  const char* postal_code;
  const char* area_code;
};

constexpr std::array<AddressProfile, 10> addresses = {{
    {"New York", "NY", "10001", "212"}, {"Los Angeles", "CA", "90001", "213"},
    {"Chicago", "IL", "60601", "312"}, {"Houston", "TX", "77001", "713"},
    {"Phoenix", "AZ", "85001", "602"}, {"Philadelphia", "PA", "19103", "215"},
    {"Seattle", "WA", "98101", "206"}, {"Denver", "CO", "80202", "303"},
    {"Boston", "MA", "02108", "617"}, {"Atlanta", "GA", "30303", "404"},
}};

std::uint64_t stable_hash(const std::string& value) {
  std::uint64_t hash = 1469598103934665603ULL;
  for (const unsigned char character : value) {
    hash ^= character;
    hash *= 1099511628211ULL;
  }
  return hash;
}

std::uint64_t mix(std::uint64_t value) {
  value += 0x9e3779b97f4a7c15ULL;
  value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
  value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
  return value ^ (value >> 31U);
}

std::uint64_t row_hash(unsigned int seed, const std::string& table_name,
                       std::size_t row_index) {
  return mix(static_cast<std::uint64_t>(seed) ^ stable_hash(table_name) ^
             mix(static_cast<std::uint64_t>(row_index + 1)));
}

std::string normalize(std::string value) {
  std::ranges::transform(value, value.begin(), [](unsigned char character) {
    return std::isalnum(character) != 0 ? static_cast<char>(std::tolower(character)) : '_';
  });
  return value;
}

bool contains(const std::string& value, const std::string& token) {
  return value.find(token) != std::string::npos;
}

std::string lowercase(std::string value) {
  std::ranges::transform(value, value.begin(), [](unsigned char character) {
    return static_cast<char>(std::tolower(character));
  });
  return value;
}

std::string fit_text(std::string value, const Column& column, bool unique,
                     std::size_t row_index) {
  const auto type = column.get_column_type();
  if (type.data_type != DataType::VARCHAR || type.length <= 0 ||
      value.size() <= static_cast<std::size_t>(type.length)) {
    return value;
  }
  const std::size_t limit = static_cast<std::size_t>(type.length);
  if (!unique) {
    value.resize(limit);
    return value;
  }
  const std::string suffix = "_" + std::to_string(row_index + 1);
  if (suffix.size() >= limit) {
    return suffix.substr(suffix.size() - limit);
  }
  value.resize(limit - suffix.size());
  return value + suffix;
}

DateValue date_from_days(int days_after_2020) {
  using namespace std::chrono;
  const sys_days day = sys_days{year{2020} / January / 1} + days{days_after_2020};
  const year_month_day calendar{day};
  return DateValue{.year = static_cast<int>(calendar.year()),
                   .month = static_cast<int>(static_cast<unsigned int>(calendar.month())),
                   .day = static_cast<int>(static_cast<unsigned int>(calendar.day()))};
}

TimeValue time_from_seconds(int seconds) {
  seconds %= 24 * 60 * 60;
  return TimeValue{.hour = seconds / 3600,
                   .minute = (seconds % 3600) / 60,
                   .second = seconds % 60};
}

bool looks_unique_name(const std::string& name) {
  return contains(name, "email") || contains(name, "username") || contains(name, "sku") ||
         contains(name, "code") || contains(name, "tracking");
}

bool is_email_column(const std::string& column_name) {
  return contains(column_name, "email") && !column_name.ends_with("_id") &&
         !column_name.ends_with("id");
}

std::string zero_padded(int value, int width) {
  std::ostringstream output;
  output << std::setw(width) << std::setfill('0') << value;
  return output.str();
}

double decimal_for_row(const Column& column, std::size_t row_index) {
  const auto type = column.get_column_type();
  const int scale = type.scale > 0 ? static_cast<int>(type.scale) : 2;
  const double factor = std::pow(10.0, std::min(scale, 6));
  double maximum = 999.99;
  if (type.precision > 0) {
    maximum = std::pow(10.0, static_cast<double>(std::max<std::int64_t>(
                                      0, type.precision - type.scale))) -
              1.0 / factor;
  }

  const double raw = static_cast<double>((row_index + 1) * 10) + 0.5;
  const double value = std::min(raw, maximum);
  return std::round(value * factor) / factor;
}

}  // namespace

double RealisticValueGenerator::deterministic_fraction(unsigned int seed,
                                                       const std::string& table_name,
                                                       const std::string& discriminator,
                                                       std::size_t row_index) {
  const std::uint64_t value = mix(row_hash(seed, table_name, row_index) ^ stable_hash(discriminator));
  return static_cast<double>(value >> 11U) / static_cast<double>(1ULL << 53U);
}

std::optional<GeneratedValue> RealisticValueGenerator::generate_heuristic(
    const Table& table, const Column& column, std::size_t row_index,
    const GenerationConfig& config, bool require_unique) {
  (void)table;
  (void)config;

  const std::string column_name = normalize(column.get_column_name());
  const DataType data_type = column.get_column_type().data_type;

  if (ColumnDomainResolver::is_text_type(data_type)) {
    std::string value;
    bool naturally_unique = false;
    if (is_email_column(column_name)) {
      value = "email_" + std::to_string(row_index + 1) + "@example.com";
      naturally_unique = true;
    } else if (column_name == "first_name" || column_name == "firstname" ||
               column_name == "given_name") {
      value = "first_name_" + std::to_string(row_index + 1);
      naturally_unique = true;
    } else if (column_name == "last_name" || column_name == "lastname" ||
               column_name == "surname" || column_name == "family_name") {
      value = "last_name_" + std::to_string(row_index + 1);
      naturally_unique = true;
    } else if (contains(column_name, "username") || column_name == "login") {
      value = "user_" + std::to_string(row_index + 1);
      naturally_unique = true;
    } else if (contains(column_name, "phone") || contains(column_name, "mobile")) {
      value = "555-000-" + zero_padded(static_cast<int>((row_index % 10000) + 1), 4);
      naturally_unique = true;
    } else if (contains(column_name, "status")) {
      constexpr std::array<const char*, 3> default_statuses = {"active", "inactive", "pending"};
      value = default_statuses[row_index % default_statuses.size()];
    } else {
      return std::nullopt;
    }

    if (require_unique && !naturally_unique) {
      value += "_" + std::to_string(row_index + 1);
    }
    return GeneratedValue::text(fit_text(std::move(value), column, require_unique || naturally_unique,
                                         row_index));
  }

  if (ColumnDomainResolver::is_decimal_type(data_type) &&
      (contains(column_name, "price") || contains(column_name, "amount"))) {
    return GeneratedValue::numeric(decimal_for_row(column, row_index));
  }

  if (data_type == DataType::DATETIME && contains(column_name, "created_at")) {
    return GeneratedValue::date_time(
        DateTimeValue{.date = date_from_days(2192 + static_cast<int>(row_index / 86400)),
                      .time = time_from_seconds(static_cast<int>(row_index % 86400))});
  }

  if (data_type == DataType::DATE && contains(column_name, "created_at")) {
    return GeneratedValue::date(date_from_days(2192 + static_cast<int>(row_index)));
  }

  if (data_type == DataType::BOOLEAN && column_name == "is_active") {
    return GeneratedValue::boolean(row_index % 2 == 0);
  }

  return std::nullopt;
}

std::optional<GeneratedValue> RealisticValueGenerator::generate(
    const Table& table, const Column& column, std::size_t row_index,
    const GenerationConfig& config, bool require_unique) {
  if (!config.realistic) {
    return generate_heuristic(table, column, row_index, config, require_unique);
  }

  const std::string column_name = normalize(column.get_column_name());
  const std::string table_name = normalize(table.get_table_name());
  const DataType data_type = column.get_column_type().data_type;
  const std::uint64_t hash = row_hash(config.seed, table.get_table_name(), row_index);
  const std::string first = first_names[hash % first_names.size()];
  const std::string last = last_names[(hash >> 8U) % last_names.size()];
  const AddressProfile& address = addresses[(hash >> 16U) % addresses.size()];
  const bool unique = require_unique || looks_unique_name(column_name);

  if (ColumnDomainResolver::is_text_type(data_type)) {
    std::string value;
    if (column_name == "first_name" || column_name == "firstname" || column_name == "given_name") {
      value = first;
    } else if (column_name == "last_name" || column_name == "lastname" ||
               column_name == "surname" || column_name == "family_name") {
      value = last;
    } else if (column_name == "full_name" ||
               (column_name == "name" &&
                (contains(table_name, "user") || contains(table_name, "customer") ||
                 contains(table_name, "person") || contains(table_name, "member"))) ||
               (contains(table_name, "user") && contains(column_name, "name"))) {
      value = first + " " + last;
    } else if (contains(column_name, "company") || contains(column_name, "organization") ||
               (column_name == "name" && (contains(table_name, "compan") ||
                                           contains(table_name, "organization")))) {
      value = companies[(hash >> 10U) % companies.size()];
    } else if (contains(column_name, "email")) {
      value = lowercase(first) + "." + lowercase(last);
      if (unique) {
        value += "+" + std::to_string(row_index + 1);
      }
      value += "@" + std::string(domains[(hash >> 24U) % domains.size()]);
    } else if (contains(column_name, "username") || column_name == "login") {
      value = lowercase(first.substr(0, 1) + last) + std::to_string(100 + (hash % 900));
      if (unique) {
        value += "_" + std::to_string(row_index + 1);
      }
    } else if (contains(column_name, "phone") || contains(column_name, "mobile")) {
      const int exchange = 200 + static_cast<int>((hash >> 24U) % 700);
      const int line = static_cast<int>((hash >> 32U) % 10000);
      value = "+1-" + std::string(address.area_code) + "-" + std::to_string(exchange) + "-";
      value += std::string(4 - std::min<std::size_t>(4, std::to_string(line).size()), '0') +
               std::to_string(line);
    } else if (column_name == "city" || contains(column_name, "city_name")) {
      value = address.city;
    } else if (column_name == "state" || column_name == "region" ||
               column_name == "state_code") {
      value = address.state;
    } else if (contains(column_name, "postal") || contains(column_name, "zip")) {
      value = address.postal_code;
    } else if (column_name == "country" || column_name == "country_code") {
      value = column_name == "country_code" ? "US" : "United States";
    } else if (contains(column_name, "address") || contains(column_name, "street")) {
      value = std::to_string(100 + (hash % 9800)) + " " + last + " Street";
    } else if (contains(column_name, "category")) {
      value = categories[(hash >> 12U) % categories.size()];
    } else if (contains(column_name, "status") || contains(column_name, "state_name")) {
      value = statuses[(hash >> 20U) % statuses.size()];
    } else if (contains(column_name, "sku")) {
      value = "SKU-" + std::to_string(100000 + row_index);
    } else if (contains(column_name, "tracking")) {
      value = "TRK" + std::to_string(100000000 + row_index);
    } else if (contains(column_name, "product") ||
               (column_name == "name" && contains(table_name, "product"))) {
      value = std::string(product_adjectives[hash % product_adjectives.size()]) + " " +
              product_nouns[(hash >> 8U) % product_nouns.size()];
    } else if (contains(column_name, "title")) {
      value = std::string(title_adjectives[hash % title_adjectives.size()]) + " " +
              product_nouns[(hash >> 8U) % product_nouns.size()];
    } else if (contains(column_name, "description") || contains(column_name, "notes") ||
               contains(column_name, "bio") || contains(column_name, "summary")) {
      value = "A " + lowercase(std::string(title_adjectives[hash % title_adjectives.size()])) +
              " description for " +
              lowercase(std::string(product_nouns[(hash >> 8U) % product_nouns.size()])) + ".";
    } else if (column_name == "name") {
      value = first + " " + last;
    } else {
      return std::nullopt;
    }
    if (require_unique && !looks_unique_name(column_name)) {
      value += "_" + std::to_string(row_index + 1);
    }
    return GeneratedValue::text(fit_text(std::move(value), column, unique, row_index));
  }

  if (ColumnDomainResolver::is_decimal_type(data_type) &&
      (contains(column_name, "price") || contains(column_name, "cost") ||
       contains(column_name, "amount") || contains(column_name, "total") ||
       contains(column_name, "balance"))) {
    const auto type = column.get_column_type();
    const int scale = type.scale > 0 ? static_cast<int>(type.scale) : 2;
    const double factor = std::pow(10.0, std::min(scale, 6));
    double maximum = 999.99;
    if (type.precision > 0) {
      maximum = std::pow(10.0, static_cast<double>(std::max<std::int64_t>(
                                        0, type.precision - type.scale))) -
                1.0 / factor;
    }
    const double raw = 1.0 + static_cast<double>(hash % 99900) / 100.0;
    const double value = std::min(raw, maximum);
    return GeneratedValue::numeric(std::round(value * factor) / factor);
  }

  if (ColumnDomainResolver::is_integer_type(data_type)) {
    if (column_name == "age") {
      return GeneratedValue::integer(18 + static_cast<std::int64_t>(hash % 63));
    }
    if (contains(column_name, "quantity") || column_name == "qty") {
      return GeneratedValue::integer(1 + static_cast<std::int64_t>(hash % 12));
    }
  }

  const int day_offset = static_cast<int>(hash % 2192);  // 2020-01-01 through 2025-12-31.
  const int seconds = static_cast<int>((hash >> 12U) % (24 * 60 * 60));
  if (data_type == DataType::DATE) {
    if (contains(column_name, "birth") || column_name == "dob") {
      return GeneratedValue::date(date_from_days(-21915 + static_cast<int>(hash % 16436)));
    }
    return GeneratedValue::date(date_from_days(day_offset));
  }
  if (data_type == DataType::TIME) {
    return GeneratedValue::time(time_from_seconds(seconds));
  }
  if (data_type == DataType::DATETIME) {
    if (contains(column_name, "created_at")) {
      return GeneratedValue::date_time(
          DateTimeValue{.date = date_from_days(2192 + static_cast<int>(row_index / 86400)),
                        .time = time_from_seconds(static_cast<int>(row_index % 86400))});
    }
    int adjusted_offset = day_offset;
    if (contains(column_name, "updated") || contains(column_name, "completed") ||
        contains(column_name, "shipped")) {
      adjusted_offset += static_cast<int>((hash >> 32U) % 30);
    }
    return GeneratedValue::date_time(
        DateTimeValue{.date = date_from_days(adjusted_offset),
                      .time = time_from_seconds(seconds)});
  }

  return generate_heuristic(table, column, row_index, config, require_unique);
}

}  // namespace schemaforge
