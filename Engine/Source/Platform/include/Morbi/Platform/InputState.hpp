#pragma once

#include <array>

#include "Morbi/Core/Types.hpp"

namespace Morbi::Platform
{
    enum class MouseButton : u8
    {
        Left = 0,
        Right = 1,
        Middle = 2,
        X1 = 3,
        X2 = 4,
        Count = 5
    };

    class InputState
    {
    public:
        static constexpr usize kMaxKeys = 512;

        void BeginFrame() noexcept;

        void SetKeyDown(u32 scancode, bool down) noexcept;
        [[nodiscard]] bool IsKeyDown(u32 scancode) const noexcept;
        [[nodiscard]] bool WasKeyPressedThisFrame(u32 scancode) const noexcept;
        [[nodiscard]] bool WasKeyReleasedThisFrame(u32 scancode) const noexcept;

        void SetMouseButtonDown(MouseButton button, bool down) noexcept;
        [[nodiscard]] bool IsMouseButtonDown(MouseButton button) const noexcept;
        [[nodiscard]] bool WasMouseButtonPressedThisFrame(MouseButton button) const noexcept;
        [[nodiscard]] bool WasMouseButtonReleasedThisFrame(MouseButton button) const noexcept;

        void SetMousePosition(f32 x, f32 y) noexcept;
        void AddMouseDelta(f32 dx, f32 dy) noexcept;
        void AddMouseWheelDelta(f32 wheelX, f32 wheelY) noexcept;

        void SetMouseCaptured(bool captured) noexcept
        {
            m_mouseCaptured = captured;
            m_cursorVisible = !captured;
        }
        [[nodiscard]] bool IsMouseCaptured() const noexcept { return m_mouseCaptured; }

        void SetCursorVisible(bool visible) noexcept { m_cursorVisible = visible; }
        [[nodiscard]] bool IsCursorVisible() const noexcept { return m_cursorVisible; }

        [[nodiscard]] f32 MouseX() const noexcept { return m_mouseX; }
        [[nodiscard]] f32 MouseY() const noexcept { return m_mouseY; }
        [[nodiscard]] f32 MouseZ() const noexcept { return m_mouseZ; }
        [[nodiscard]] f32 MouseDeltaX() const noexcept { return m_mouseDeltaX; }
        [[nodiscard]] f32 MouseDeltaY() const noexcept { return m_mouseDeltaY; }
        [[nodiscard]] f32 MouseDeltaZ() const noexcept { return m_mouseWheelY; }
        [[nodiscard]] f32 MouseWheelX() const noexcept { return m_mouseWheelX; }
        [[nodiscard]] f32 MouseWheelY() const noexcept { return m_mouseWheelY; }

    private:
        std::array<bool, kMaxKeys> m_currentKeys{};
        std::array<bool, kMaxKeys> m_previousKeys{};
        std::array<bool, static_cast<usize>(MouseButton::Count)> m_currentMouseButtons{};
        std::array<bool, static_cast<usize>(MouseButton::Count)> m_previousMouseButtons{};

        f32 m_mouseX = 0.0f;
        f32 m_mouseY = 0.0f;
        f32 m_mouseZ = 0.0f;
        f32 m_mouseDeltaX = 0.0f;
        f32 m_mouseDeltaY = 0.0f;
        f32 m_mouseWheelX = 0.0f;
        f32 m_mouseWheelY = 0.0f;
        bool m_mouseCaptured = false;
        bool m_cursorVisible = true;
    };
}
