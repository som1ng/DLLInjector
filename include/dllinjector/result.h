#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <variant>

namespace dllinjector {
enum class ErrorCategory { InvalidArgument, AccessDenied, NotFound, Unsupported, System, Timeout, Io };

struct Error {
    ErrorCategory category;
    std::string stage;
    std::string message;
    std::optional<std::uint32_t> win32;
    std::optional<std::int32_t> ntstatus;
    bool verified = true;
};

template<class T> class Result {
public:
    Result(T value) : result_(std::move(value)) {}
    Result(Error error) : result_(std::move(error)) {}
    explicit operator bool() const { return std::holds_alternative<T>(result_); }
    const T& value() const { return std::get<T>(result_); }
    T& value() { return std::get<T>(result_); }
    const Error& error() const { return std::get<Error>(result_); }
private:
    std::variant<T, Error> result_;
};

const char* CategoryName(ErrorCategory category);
int ExitCode(ErrorCategory category);
}
