#pragma once

#include <string>
#include <utility>

namespace spriteloop {

enum class SplaErrorCode {
    none,
    file_not_found,
    package_error,
    manifest_missing,
    manifest_invalid,
    unsupported_format,
    unsupported_version,
    validation_error,
};

struct SplaError {
    SplaErrorCode code = SplaErrorCode::none;
    std::string message;

    [[nodiscard]] bool ok() const noexcept
    {
        return code == SplaErrorCode::none;
    }
};

template <typename T>
class SplaResult {
public:
    SplaResult(T value)
        : value_(std::move(value))
    {
    }

    SplaResult(SplaError error)
        : error_(std::move(error))
    {
    }

    [[nodiscard]] bool ok() const noexcept
    {
        return error_.ok();
    }

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return ok();
    }

    [[nodiscard]] const T& value() const&
    {
        return value_;
    }

    [[nodiscard]] T& value() &
    {
        return value_;
    }

    [[nodiscard]] T&& value() &&
    {
        return std::move(value_);
    }

    [[nodiscard]] const SplaError& error() const noexcept
    {
        return error_;
    }

private:
    T value_{};
    SplaError error_;
};

} // namespace spriteloop
