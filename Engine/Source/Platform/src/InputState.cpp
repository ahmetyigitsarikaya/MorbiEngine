#include "Morbi/Platform/InputState.hpp"

namespace Morbi::Platform
{
    void InputState::BeginFrame() noexcept
    {
        m_previousKeys = m_currentKeys;
        m_previousMouseButtons = m_currentMouseButtons;
        m_mouseDeltaX = 0.0f;
        m_mouseDeltaY = 0.0f;
        m_mouseWheelX = 0.0f;
        m_mouseWheelY = 0.0f;
    }

    void InputState::SetKeyDown(u32 scancode, bool down) noexcept
    {
        if (scancode < kMaxKeys)
        {
            m_currentKeys[scancode] = down;
        }
    }

    bool InputState::IsKeyDown(u32 scancode) const noexcept
    {
        return scancode < kMaxKeys ? m_currentKeys[scancode] : false;
    }

    bool InputState::WasKeyPressedThisFrame(u32 scancode) const noexcept
    {
        return scancode < kMaxKeys ? (m_currentKeys[scancode] && !m_previousKeys[scancode]) : false;
    }

    bool InputState::WasKeyReleasedThisFrame(u32 scancode) const noexcept
    {
        return scancode < kMaxKeys ? (!m_currentKeys[scancode] && m_previousKeys[scancode]) : false;
    }

    void InputState::SetMouseButtonDown(MouseButton button, bool down) noexcept
    {
        const auto idx = static_cast<usize>(button);
        if (idx < m_currentMouseButtons.size())
        {
            m_currentMouseButtons[idx] = down;
        }
    }

    bool InputState::IsMouseButtonDown(MouseButton button) const noexcept
    {
        const auto idx = static_cast<usize>(button);
        return idx < m_currentMouseButtons.size() ? m_currentMouseButtons[idx] : false;
    }

    bool InputState::WasMouseButtonPressedThisFrame(MouseButton button) const noexcept
    {
        const auto idx = static_cast<usize>(button);
        return idx < m_currentMouseButtons.size()
            ? (m_currentMouseButtons[idx] && !m_previousMouseButtons[idx])
            : false;
    }

    bool InputState::WasMouseButtonReleasedThisFrame(MouseButton button) const noexcept
    {
        const auto idx = static_cast<usize>(button);
        return idx < m_currentMouseButtons.size()
            ? (!m_currentMouseButtons[idx] && m_previousMouseButtons[idx])
            : false;
    }

    void InputState::SetMousePosition(f32 x, f32 y) noexcept
    {
        m_mouseX = x;
        m_mouseY = y;
    }

    void InputState::AddMouseDelta(f32 dx, f32 dy) noexcept
    {
        m_mouseDeltaX += dx;
        m_mouseDeltaY += dy;
    }

    void InputState::AddMouseWheelDelta(f32 wheelX, f32 wheelY) noexcept
    {
        m_mouseWheelX += wheelX;
        m_mouseWheelY += wheelY;
        m_mouseZ += wheelY;
    }
}
