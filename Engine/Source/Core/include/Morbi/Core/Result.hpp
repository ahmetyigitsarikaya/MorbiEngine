#pragma once

#include <utility>

namespace Morbi::Core
{
    template <typename TValue, typename TError>
    class Result
    {
    public:
        [[nodiscard]] static Result Success(TValue value)
        {
            return Result(true, std::move(value), TError{});
        }

        [[nodiscard]] static Result Failure(TError error)
        {
            return Result(false, TValue{}, std::move(error));
        }

        [[nodiscard]] constexpr bool IsSuccess() const noexcept
        {
            return m_success;
        }

        [[nodiscard]] constexpr explicit operator bool() const noexcept
        {
            return m_success;
        }

        [[nodiscard]] const TValue& Value() const & noexcept
        {
            return m_value;
        }

        [[nodiscard]] TValue& Value() & noexcept
        {
            return m_value;
        }

        [[nodiscard]] TValue&& ConsumeValue() && noexcept
        {
            return std::move(m_value);
        }

        [[nodiscard]] const TError& Error() const noexcept
        {
            return m_error;
        }

    private:
        Result(bool success, TValue value, TError error)
            : m_success(success)
            , m_value(std::move(value))
            , m_error(std::move(error))
        {
        }

        bool m_success = false;
        TValue m_value{};
        TError m_error{};
    };
}
