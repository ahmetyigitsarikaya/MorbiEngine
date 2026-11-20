#include "Morbi/Platform/Window.hpp"

#include <SDL3/SDL.h>

#include "Morbi/Core/Log.hpp"

namespace Morbi::Platform
{
    namespace
    {
        MouseButton TranslateSDLMouseButton(u8 sdlButton) noexcept
        {
            switch (sdlButton)
            {
            case SDL_BUTTON_LEFT:   return MouseButton::Left;
            case SDL_BUTTON_RIGHT:  return MouseButton::Right;
            case SDL_BUTTON_MIDDLE: return MouseButton::Middle;
            case SDL_BUTTON_X1:     return MouseButton::X1;
            case SDL_BUTTON_X2:     return MouseButton::X2;
            default:                return MouseButton::Left;
            }
        }
    }

    struct Window::Impl
    {
        SDL_Window* SdlWindow = nullptr;
        u32 Width = 0;
        u32 Height = 0;
        f32 DpiScale = 1.0f;
        bool Open = false;
        bool Minimized = false;
        bool MouseCaptured = false;
        bool CursorVisible = true;
        bool HasPrevGlobalMouse = false;
        f32 PrevGlobalMouseX = 0.0f;
        f32 PrevGlobalMouseY = 0.0f;
    };

    Window::Window()
        : m_impl(std::make_unique<Impl>())
    {
    }

    Window::~Window()
    {
        Destroy();
    }

    Core::Result<bool, std::string> Window::Create(const WindowDesc& desc)
    {
        if (m_impl->Open)
        {
            return Core::Result<bool, std::string>::Failure("Window is already open.");
        }

        if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS))
        {
            return Core::Result<bool, std::string>::Failure(
                std::string("SDL_Init failed: ") + SDL_GetError());
        }

        SDL_WindowFlags flags = 0;
        if (desc.HighDPI)
        {
            flags |= SDL_WINDOW_HIGH_PIXEL_DENSITY;
        }
        if (desc.Resizable)
        {
            flags |= SDL_WINDOW_RESIZABLE;
        }
        if (!desc.Visible)
        {
            flags |= SDL_WINDOW_HIDDEN;
        }

        m_impl->SdlWindow = SDL_CreateWindow(
            desc.Title.c_str(),
            static_cast<int>(desc.Width),
            static_cast<int>(desc.Height),
            flags);

        if (m_impl->SdlWindow == nullptr)
        {
            std::string err = std::string("SDL_CreateWindow failed: ") + SDL_GetError();
            SDL_Quit();
            return Core::Result<bool, std::string>::Failure(std::move(err));
        }

        int pixelW = static_cast<int>(desc.Width);
        int pixelH = static_cast<int>(desc.Height);
        SDL_GetWindowSizeInPixels(m_impl->SdlWindow, &pixelW, &pixelH);

        m_impl->Width = static_cast<u32>(pixelW);
        m_impl->Height = static_cast<u32>(pixelH);
        m_impl->DpiScale = SDL_GetWindowDisplayScale(m_impl->SdlWindow);
        if (m_impl->DpiScale <= 0.0f)
        {
            m_impl->DpiScale = 1.0f;
        }
        m_impl->Open = true;
        m_impl->Minimized = false;

        Core::Log::Info("Platform", "Created SDL3 window '" + desc.Title + "' (" +
            std::to_string(m_impl->Width) + "x" + std::to_string(m_impl->Height) +
            ", DPI scale=" + std::to_string(m_impl->DpiScale) + ")");

        return Core::Result<bool, std::string>::Success(true);
    }

    void Window::Destroy()
    {
        if (m_impl->SdlWindow != nullptr)
        {
            SDL_DestroyWindow(m_impl->SdlWindow);
            m_impl->SdlWindow = nullptr;
        }

        if (m_impl->Open)
        {
            SDL_Quit();
            m_impl->Open = false;
        }
    }

    void Window::PumpEvents(InputState& input)
    {
        if (input.IsMouseCaptured() != m_impl->MouseCaptured)
        {
            SetMouseCaptured(input.IsMouseCaptured());
        }
        if (input.IsCursorVisible() != m_impl->CursorVisible)
        {
            SetCursorVisible(input.IsCursorVisible());
        }

        input.BeginFrame();
        bool receivedMotionEvent = false;

        SDL_Event ev;
        while (SDL_PollEvent(&ev))
        {
            switch (ev.type)
            {
            case SDL_EVENT_QUIT:
                m_impl->Open = false;
                break;

            case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                if (m_impl->SdlWindow && ev.window.windowID == SDL_GetWindowID(m_impl->SdlWindow))
                {
                    m_impl->Open = false;
                }
                break;

            case SDL_EVENT_WINDOW_RESIZED:
            case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
                if (m_impl->SdlWindow && ev.window.windowID == SDL_GetWindowID(m_impl->SdlWindow))
                {
                    int w = 0, h = 0;
                    SDL_GetWindowSizeInPixels(m_impl->SdlWindow, &w, &h);
                    m_impl->Width = static_cast<u32>(w);
                    m_impl->Height = static_cast<u32>(h);
                }
                break;

            case SDL_EVENT_WINDOW_MINIMIZED:
                if (m_impl->SdlWindow && ev.window.windowID == SDL_GetWindowID(m_impl->SdlWindow))
                {
                    m_impl->Minimized = true;
                }
                break;

            case SDL_EVENT_WINDOW_RESTORED:
                if (m_impl->SdlWindow && ev.window.windowID == SDL_GetWindowID(m_impl->SdlWindow))
                {
                    m_impl->Minimized = false;
                }
                break;

            case SDL_EVENT_KEY_DOWN:
                input.SetKeyDown(static_cast<u32>(ev.key.scancode), true);
                break;

            case SDL_EVENT_KEY_UP:
                input.SetKeyDown(static_cast<u32>(ev.key.scancode), false);
                break;

            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                input.SetMouseButtonDown(TranslateSDLMouseButton(ev.button.button), true);
                break;

            case SDL_EVENT_MOUSE_BUTTON_UP:
                input.SetMouseButtonDown(TranslateSDLMouseButton(ev.button.button), false);
                break;

            case SDL_EVENT_MOUSE_MOTION:
                input.SetMousePosition(ev.motion.x, ev.motion.y);
                input.AddMouseDelta(ev.motion.xrel, ev.motion.yrel);
                receivedMotionEvent = true;
                break;

            case SDL_EVENT_MOUSE_WHEEL:
                input.AddMouseWheelDelta(ev.wheel.x, ev.wheel.y);
                break;

            default:
                break;
            }
        }

        // Fallback global mouse delta tracking (ensures embedded Win32 child viewports always produce smooth X/Y deltas)
        f32 globalX = 0.0f;
        f32 globalY = 0.0f;
        SDL_GetGlobalMouseState(&globalX, &globalY);
        if (m_impl->HasPrevGlobalMouse && !receivedMotionEvent)
        {
            const f32 dx = globalX - m_impl->PrevGlobalMouseX;
            const f32 dy = globalY - m_impl->PrevGlobalMouseY;
            if (dx != 0.0f || dy != 0.0f)
            {
                input.AddMouseDelta(dx, dy);
            }
        }
        m_impl->PrevGlobalMouseX = globalX;
        m_impl->PrevGlobalMouseY = globalY;
        m_impl->HasPrevGlobalMouse = true;

        input.SetMouseCaptured(m_impl->MouseCaptured);
        input.SetCursorVisible(m_impl->CursorVisible);
    }

    bool Window::IsOpen() const noexcept { return m_impl->Open; }
    bool Window::IsMinimized() const noexcept { return m_impl->Minimized; }
    u32 Window::Width() const noexcept { return m_impl->Width; }
    u32 Window::Height() const noexcept { return m_impl->Height; }
    f32 Window::DpiScale() const noexcept { return m_impl->DpiScale; }

    NativeWindowHandle Window::GetNativeHandle() const noexcept
    {
        NativeWindowHandle handle{};
        if (m_impl->SdlWindow == nullptr)
        {
            return handle;
        }

        const SDL_PropertiesID props = SDL_GetWindowProperties(m_impl->SdlWindow);
        if (void* hwnd = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr))
        {
            handle.Backend = NativeWindowBackend::Win32;
            handle.Window = hwnd;
            handle.DisplayOrInstance = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WIN32_INSTANCE_POINTER, nullptr);
        }
        return handle;
    }

    void Window::ApplyWindowSettings(Core::WindowMode mode, u32 width, u32 height)
    {
        if (m_impl->SdlWindow == nullptr)
        {
            return;
        }

        if (mode == Core::WindowMode::Fullscreen)
        {
            SDL_SetWindowFullscreen(m_impl->SdlWindow, true);
        }
        else if (mode == Core::WindowMode::Borderless)
        {
            SDL_SetWindowFullscreen(m_impl->SdlWindow, false);
            SDL_SetWindowBordered(m_impl->SdlWindow, false);
            SDL_SetWindowSize(m_impl->SdlWindow, static_cast<int>(width), static_cast<int>(height));
        }
        else
        {
            SDL_SetWindowFullscreen(m_impl->SdlWindow, false);
            SDL_SetWindowBordered(m_impl->SdlWindow, true);
            SDL_SetWindowSize(m_impl->SdlWindow, static_cast<int>(width), static_cast<int>(height));
        }

        int pixelW = static_cast<int>(width);
        int pixelH = static_cast<int>(height);
        SDL_GetWindowSizeInPixels(m_impl->SdlWindow, &pixelW, &pixelH);
        m_impl->Width = static_cast<u32>(pixelW > 0 ? pixelW : static_cast<int>(width));
        m_impl->Height = static_cast<u32>(pixelH > 0 ? pixelH : static_cast<int>(height));
    }

    void Window::SetMouseCaptured(bool captured)
    {
        m_impl->MouseCaptured = captured;
        m_impl->CursorVisible = !captured;
        if (m_impl->SdlWindow != nullptr)
        {
            SDL_SetWindowRelativeMouseMode(m_impl->SdlWindow, captured);
            SDL_SetWindowMouseGrab(m_impl->SdlWindow, captured);
        }
        if (captured)
        {
            SDL_HideCursor();
        }
        else
        {
            SDL_ShowCursor();
        }
    }

    bool Window::IsMouseCaptured() const noexcept
    {
        return m_impl->MouseCaptured;
    }

    void Window::SetCursorVisible(bool visible)
    {
        m_impl->CursorVisible = visible;
        if (visible)
        {
            SDL_ShowCursor();
        }
        else
        {
            SDL_HideCursor();
        }
    }

    bool Window::IsCursorVisible() const noexcept
    {
        return m_impl->CursorVisible;
    }

    void Window::RequestClose() noexcept
    {
        m_impl->Open = false;
    }
}
