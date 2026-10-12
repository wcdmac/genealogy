#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <variant>

namespace genealogy {

// 错误码与 REST problem+json 的 code 字段对齐（设计文档 5.2.1）。
enum class ErrorCode : int {
  Ok = 0,
  InvalidArgument = 1001,
  NotFound = 2001,
  Conflict = 2009,
  Unauthorized = 3001,
  Forbidden = 4001,
  CalendarOutOfRange = 5001,
  IoError = 6001,
  Internal = 9001,
};

inline const char* error_code_name(ErrorCode code) noexcept {
  switch (code) {
    case ErrorCode::Ok: return "ok";
    case ErrorCode::InvalidArgument: return "invalid_argument";
    case ErrorCode::NotFound: return "not_found";
    case ErrorCode::Conflict: return "conflict";
    case ErrorCode::Unauthorized: return "unauthorized";
    case ErrorCode::Forbidden: return "forbidden";
    case ErrorCode::CalendarOutOfRange: return "calendar_out_of_range";
    case ErrorCode::IoError: return "io_error";
    case ErrorCode::Internal: return "internal";
  }
  return "unknown";
}

class Error {
 public:
  Error() = default;
  Error(ErrorCode code, std::string message)
      : code_(code), message_(std::move(message)) {}

  ErrorCode code() const noexcept { return code_; }
  const std::string& message() const noexcept { return message_; }

  std::string to_string() const {
    return std::string(error_code_name(code_)) + ": " + message_;
  }

  explicit operator bool() const noexcept { return code_ != ErrorCode::Ok; }

 private:
  ErrorCode code_ = ErrorCode::Ok;
  std::string message_;
};

// 轻量 expected 实现（C++20 兼容，core 不引第三方）。
// 注意：error() 仅在 has_value() == false 时可用。
template <typename T>
class Result {
 public:
  Result(T value) : data_(std::move(value)) {}   // NOLINT
  Result(Error error) : data_(std::move(error)) {}  // NOLINT

  bool has_value() const noexcept { return std::holds_alternative<T>(data_); }
  explicit operator bool() const noexcept { return has_value(); }

  T& value() { return std::get<T>(data_); }
  const T& value() const { return std::get<T>(data_); }
  T& operator*() { return value(); }
  const T& operator*() const { return value(); }
  T* operator->() { return &value(); }
  const T* operator->() const { return &value(); }

  Error& error() { return std::get<Error>(data_); }
  const Error& error() const { return std::get<Error>(data_); }

 private:
  std::variant<T, Error> data_;
};

template <>
class Result<void> {
 public:
  Result() = default;
  Result(Error error) : error_(std::move(error)) {}  // NOLINT

  bool has_value() const noexcept { return !static_cast<bool>(error_); }
  explicit operator bool() const noexcept { return has_value(); }

  Error& error() noexcept { return error_; }
  const Error& error() const noexcept { return error_; }

 private:
  Error error_;
};

inline Error invalid_argument(std::string message) {
  return Error{ErrorCode::InvalidArgument, std::move(message)};
}

inline Error not_found(std::string message) {
  return Error{ErrorCode::NotFound, std::move(message)};
}

inline Error conflict(std::string message) {
  return Error{ErrorCode::Conflict, std::move(message)};
}

inline Error internal(std::string message) {
  return Error{ErrorCode::Internal, std::move(message)};
}

}  // namespace genealogy
