#pragma once

#include <filesystem>
#include <functional>
#include <mutex>
#include <vector>

#include "Morbi/Core/Types.hpp"

namespace Morbi::Core
{
    enum class WindowMode : u8
    {
        Windowed = 0,
        Borderless = 1,
        Fullscreen = 2
    };

    enum class ShadowQuality : u8
    {
        Off = 0,
        Low = 1,
        Medium = 2,
        High = 3,
        Ultra = 4
    };

    enum class SettingsDirtyFlags : u32
    {
        None            = 0,
        VSync           = 1u << 0,
        WindowMode      = 1u << 1,
        Resolution      = 1u << 2,
        TargetFramerate = 1u << 3,
        MSAA            = 1u << 4,
        ShadowQuality   = 1u << 5,
        All             = 0xFFFFFFFFu
    };

    [[nodiscard]] constexpr SettingsDirtyFlags operator|(SettingsDirtyFlags a, SettingsDirtyFlags b) noexcept
    {
        return static_cast<SettingsDirtyFlags>(static_cast<u32>(a) | static_cast<u32>(b));
    }

    [[nodiscard]] constexpr bool HasDirtyFlag(SettingsDirtyFlags mask, SettingsDirtyFlags flag) noexcept
    {
        return (static_cast<u32>(mask) & static_cast<u32>(flag)) != 0;
    }

    // POD / C# Blittable struct for zero-overhead interop
    struct GraphicsSettingsData
    {
        bool VSyncEnabled = true;
        WindowMode Mode = WindowMode::Windowed;
        u8 MsaaSamples = 1;
        ShadowQuality Shadows = ShadowQuality::High;
        u32 ResolutionWidth = 1280;
        u32 ResolutionHeight = 720;
        i32 TargetFramerate = -1; // -1 = unlimited

        [[nodiscard]] static constexpr GraphicsSettingsData Defaults() noexcept
        {
            return GraphicsSettingsData{};
        }
    };

    class MORBI_API EngineSettings
    {
    public:
        using ChangeCallback = std::function<void(const GraphicsSettingsData& current, SettingsDirtyFlags changedFlags)>;

        EngineSettings() noexcept = default;
        EngineSettings(const EngineSettings&) = delete;
        EngineSettings& operator=(const EngineSettings&) = delete;

        [[nodiscard]] static EngineSettings& Global() noexcept;

        void ResetToDefaults();
        bool LoadFromJsonFile(const std::filesystem::path& filePath);
        bool SaveToJsonFile(const std::filesystem::path& filePath) const;

        [[nodiscard]] GraphicsSettingsData Snapshot() const noexcept;

        // Instant Application Setters (Trigger registered RHI/Window/Renderer listeners immediately)
        void SetVSync(bool enabled);
        void SetWindowMode(WindowMode mode);
        void SetResolution(u32 width, u32 height);
        void SetTargetFramerate(i32 fps);
        void SetMsaaSamples(u8 samples);
        void SetShadowQuality(ShadowQuality quality);
        void ApplyBatch(const GraphicsSettingsData& newSettings);

        [[nodiscard]] bool IsVSyncEnabled() const noexcept;
        [[nodiscard]] WindowMode GetWindowMode() const noexcept;
        [[nodiscard]] u32 GetResolutionWidth() const noexcept;
        [[nodiscard]] u32 GetResolutionHeight() const noexcept;
        [[nodiscard]] i32 GetTargetFramerate() const noexcept;
        [[nodiscard]] u8 GetMsaaSamples() const noexcept;
        [[nodiscard]] ShadowQuality GetShadowQuality() const noexcept;

        u32 RegisterListener(ChangeCallback callback);
        void ClearListeners();

    private:
        void NotifyListeners(const GraphicsSettingsData& snapshot, SettingsDirtyFlags flags);

        struct ListenerEntry
        {
            u32 Id = 0;
            ChangeCallback Callback;
        };

        mutable std::mutex m_mutex;
        GraphicsSettingsData m_data = GraphicsSettingsData::Defaults();
        std::vector<ListenerEntry> m_listeners;
        u32 m_nextListenerId = 1;
    };
}

// ============================================================================
// C# (.NET 8 UnmanagedCallersOnly / P/Invoke) Compatible Unmanaged Exports
// ============================================================================
#if defined(_WIN32)
    #define MORBI_C_API extern "C" __declspec(dllexport)
#else
    #define MORBI_C_API extern "C" __attribute__((visibility("default")))
#endif

MORBI_C_API void Morbi_Settings_ResetDefaults();
MORBI_C_API void Morbi_Settings_GetSnapshot(Morbi::Core::GraphicsSettingsData* outSnapshot);
MORBI_C_API void Morbi_Settings_SetVSync(bool enabled);
MORBI_C_API bool Morbi_Settings_GetVSync();
MORBI_C_API void Morbi_Settings_SetWindowMode(Morbi::u8 mode);
MORBI_C_API Morbi::u8 Morbi_Settings_GetWindowMode();
MORBI_C_API void Morbi_Settings_SetResolution(Morbi::u32 width, Morbi::u32 height);
MORBI_C_API void Morbi_Settings_SetTargetFramerate(Morbi::i32 fps);
MORBI_C_API Morbi::i32 Morbi_Settings_GetTargetFramerate();
MORBI_C_API void Morbi_Settings_SetMsaaSamples(Morbi::u8 samples);
MORBI_C_API void Morbi_Settings_SetShadowQuality(Morbi::u8 quality);
