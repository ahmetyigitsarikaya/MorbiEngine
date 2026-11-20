#pragma once

#include <memory>
#include <string>
#include <string_view>

#include "Morbi/Core/EngineSettings.hpp"
#include "Morbi/Core/Result.hpp"
#include "Morbi/Core/Types.hpp"
#include "Morbi/Platform/InputState.hpp"

namespace Morbi::Platform
{
    enum class NativeWindowBackend : u8
    {
        Unknown = 0,
        Win32,
        Cocoa,
        Wayland,
        X11
    };

    struct NativeWindowHandle
    {
        NativeWindowBackend Backend = NativeWindowBackend::Unknown;
        void* Window = nullptr;
        void* DisplayOrInstance = nullptr;
    };

    struct WindowDesc
    {
        std::string Title = "MorbiEngine";
        u32 Width = 1280;
        u32 Height = 720;
        bool Resizable = true;
        bool Visible = true;
        bool HighDPI = true;
    };

    class MORBI_API Window
    {
    public:
        Window();
        ~Window();

        Window(const Window&) = delete;
        Window& operator=(const Window&) = delete;

        [[nodiscard]] Core::Result<bool, std::string> Create(const WindowDesc& desc);
        void Destroy();

        void PumpEvents(InputState& input);

        [[nodiscard]] bool IsOpen() const noexcept;
        [[nodiscard]] bool IsMinimized() const noexcept;
        [[nodiscard]] u32 Width() const noexcept;
        [[nodiscard]] u32 Height() const noexcept;
        [[nodiscard]] f32 DpiScale() const noexcept;
        [[nodiscard]] NativeWindowHandle GetNativeHandle() const noexcept;
        void ApplyWindowSettings(Core::WindowMode mode, u32 width, u32 height);
        void SetMouseCaptured(bool captured);
        [[nodiscard]] bool IsMouseCaptured() const noexcept;
        void SetCursorVisible(bool visible);
        [[nodiscard]] bool IsCursorVisible() const noexcept;
        void RequestClose() noexcept;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_impl;
    };
}
