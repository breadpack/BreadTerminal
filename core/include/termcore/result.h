#pragma once

#include <optional>
#include <string>
#include <variant>

namespace termcore {

// Lightweight error type
struct Error {
    std::string message;

    explicit Error(std::string msg) : message(std::move(msg)) {}
};

// Result<T> -- either a value or an error
template<typename T>
class Result {
public:
    // Success constructor
    Result(T value) : data_(std::move(value)) {}

    // Error constructor
    Result(Error error) : data_(std::move(error)) {}

    bool ok() const { return std::holds_alternative<T>(data_); }
    bool hasError() const { return std::holds_alternative<Error>(data_); }

    const T& value() const { return std::get<T>(data_); }
    T& value() { return std::get<T>(data_); }
    T valueOr(T fallback) const { return ok() ? value() : fallback; }

    const Error& error() const { return std::get<Error>(data_); }
    const std::string& errorMessage() const { return error().message; }

private:
    std::variant<T, Error> data_;
};

// Specialization for void results
template<>
class Result<void> {
public:
    Result() : error_(std::nullopt) {}
    Result(Error error) : error_(std::move(error)) {}

    bool ok() const { return !error_.has_value(); }
    bool hasError() const { return error_.has_value(); }
    const Error& error() const { return *error_; }
    const std::string& errorMessage() const { return error_->message; }

private:
    std::optional<Error> error_;
};

} // namespace termcore
