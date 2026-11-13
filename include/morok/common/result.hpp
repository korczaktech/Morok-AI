#pragma once

#include "morok/common/error.hpp"

#include <utility>
#include <variant>

namespace morok::common {

template <typename T>
class Result final {
public:
    [[nodiscard]] static Result success(T value) {
        return Result(std::in_place_index<0>, std::move(value));
    }

    [[nodiscard]] static Result failure(Error error) {
        return Result(std::in_place_index<1>, std::move(error));
    }

    [[nodiscard]] bool has_value() const noexcept {
        return std::holds_alternative<T>(storage_);
    }

    [[nodiscard]] explicit operator bool() const noexcept {
        return has_value();
    }

    [[nodiscard]] T* value_if() noexcept {
        return std::get_if<T>(&storage_);
    }

    [[nodiscard]] const T* value_if() const noexcept {
        return std::get_if<T>(&storage_);
    }

    [[nodiscard]] Error* error_if() noexcept {
        return std::get_if<Error>(&storage_);
    }

    [[nodiscard]] const Error* error_if() const noexcept {
        return std::get_if<Error>(&storage_);
    }

private:
    template <typename... Args>
    explicit Result(std::in_place_index_t<0>, Args&&... args)
        : storage_(std::in_place_index<0>, std::forward<Args>(args)...) {}

    template <typename... Args>
    explicit Result(std::in_place_index_t<1>, Args&&... args)
        : storage_(std::in_place_index<1>, std::forward<Args>(args)...) {}

    std::variant<T, Error> storage_;
};

template <>
class Result<void> final {
public:
    [[nodiscard]] static Result success() {
        return Result(true, Error{});
    }

    [[nodiscard]] static Result failure(Error error) {
        return Result(false, std::move(error));
    }

    [[nodiscard]] bool has_value() const noexcept {
        return success_;
    }

    [[nodiscard]] explicit operator bool() const noexcept {
        return has_value();
    }

    [[nodiscard]] Error* error_if() noexcept {
        return success_ ? nullptr : &error_;
    }

    [[nodiscard]] const Error* error_if() const noexcept {
        return success_ ? nullptr : &error_;
    }

private:
    Result(bool success, Error error)
        : success_(success), error_(std::move(error)) {}

    bool success_{false};
    Error error_{};
};

using Status = Result<void>;

} // namespace morok::common
