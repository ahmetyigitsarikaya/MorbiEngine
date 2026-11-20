#include "Morbi/Core/EngineSettings.hpp"

#include <fstream>
#include <nlohmann/json.hpp>

#include "Morbi/Core/Log.hpp"

namespace Morbi::Core
{
    namespace
    {
        const char* WindowModeToString(WindowMode mode) noexcept
        {
            switch (mode)
            {
            case WindowMode::Windowed:   return "Windowed";
            case WindowMode::Borderless: return "Borderless";
            case WindowMode::Fullscreen: return "Fullscreen";
            }
            return "Windowed";
        }

        WindowMode StringToWindowMode(std::string_view str) noexcept
        {
            if (str == "Borderless" || str == "borderless") return WindowMode::Borderless;
            if (str == "Fullscreen" || str == "fullscreen") return WindowMode::Fullscreen;
            return WindowMode::Windowed;
        }

        const char* ShadowQualityToString(ShadowQuality q) noexcept
        {
            switch (q)
            {
            case ShadowQuality::Off:    return "Off";
            case ShadowQuality::Low:    return "Low";
            case ShadowQuality::Medium: return "Medium";
            case ShadowQuality::High:   return "High";
            case ShadowQuality::Ultra:  return "Ultra";
            }
            return "High";
        }

        ShadowQuality StringToShadowQuality(std::string_view str) noexcept
        {
            if (str == "Off" || str == "off")       return ShadowQuality::Off;
            if (str == "Low" || str == "low")       return ShadowQuality::Low;
            if (str == "Medium" || str == "medium") return ShadowQuality::Medium;
            if (str == "Ultra" || str == "ultra")   return ShadowQuality::Ultra;
            return ShadowQuality::High;
        }
    }

    EngineSettings& EngineSettings::Global() noexcept
    {
        static EngineSettings s_instance;
        return s_instance;
    }

    void EngineSettings::ResetToDefaults()
    {
        ApplyBatch(GraphicsSettingsData::Defaults());
    }

    bool EngineSettings::LoadFromJsonFile(const std::filesystem::path& filePath)
    {
        std::ifstream file(filePath);
        if (!file)
        {
            return false;
        }

        nlohmann::json j = nlohmann::json::parse(file, nullptr, false);
        if (j.is_discarded())
        {
            return false;
        }

        GraphicsSettingsData loaded = Snapshot();
        if (j.contains("vsync_enabled") && j["vsync_enabled"].is_boolean())
        {
            loaded.VSyncEnabled = j["vsync_enabled"].get<bool>();
        }
        if (j.contains("window_mode") && j["window_mode"].is_string())
        {
            loaded.Mode = StringToWindowMode(j["window_mode"].get<std::string>());
        }
        if (j.contains("resolution_width") && j["resolution_width"].is_number_unsigned())
        {
            loaded.ResolutionWidth = j["resolution_width"].get<u32>();
        }
        if (j.contains("resolution_height") && j["resolution_height"].is_number_unsigned())
        {
            loaded.ResolutionHeight = j["resolution_height"].get<u32>();
        }
        if (j.contains("target_framerate") && j["target_framerate"].is_number_integer())
        {
            loaded.TargetFramerate = j["target_framerate"].get<i32>();
        }
        if (j.contains("msaa_samples") && j["msaa_samples"].is_number_unsigned())
        {
            loaded.MsaaSamples = static_cast<u8>(j["msaa_samples"].get<u32>());
        }
        if (j.contains("shadow_quality") && j["shadow_quality"].is_string())
        {
            loaded.Shadows = StringToShadowQuality(j["shadow_quality"].get<std::string>());
        }

        ApplyBatch(loaded);
        return true;
    }

    bool EngineSettings::SaveToJsonFile(const std::filesystem::path& filePath) const
    {
        const GraphicsSettingsData snap = Snapshot();
        nlohmann::json j;
        j["vsync_enabled"] = snap.VSyncEnabled;
        j["window_mode"] = WindowModeToString(snap.Mode);
        j["resolution_width"] = snap.ResolutionWidth;
        j["resolution_height"] = snap.ResolutionHeight;
        j["target_framerate"] = snap.TargetFramerate;
        j["msaa_samples"] = snap.MsaaSamples;
        j["shadow_quality"] = ShadowQualityToString(snap.Shadows);

        if (filePath.has_parent_path())
        {
            std::error_code ec;
            std::filesystem::create_directories(filePath.parent_path(), ec);
        }

        std::ofstream out(filePath);
        if (!out)
        {
            return false;
        }
        out << j.dump(4);
        return out.good();
    }

    GraphicsSettingsData EngineSettings::Snapshot() const noexcept
    {
        std::scoped_lock lock(m_mutex);
        return m_data;
    }

    void EngineSettings::SetVSync(bool enabled)
    {
        GraphicsSettingsData snap{};
        {
            std::scoped_lock lock(m_mutex);
            if (m_data.VSyncEnabled == enabled) return;
            m_data.VSyncEnabled = enabled;
            snap = m_data;
        }
        NotifyListeners(snap, SettingsDirtyFlags::VSync);
    }

    void EngineSettings::SetWindowMode(WindowMode mode)
    {
        GraphicsSettingsData snap{};
        {
            std::scoped_lock lock(m_mutex);
            if (m_data.Mode == mode) return;
            m_data.Mode = mode;
            snap = m_data;
        }
        NotifyListeners(snap, SettingsDirtyFlags::WindowMode);
    }

    void EngineSettings::SetResolution(u32 width, u32 height)
    {
        if (width == 0 || height == 0) return;
        GraphicsSettingsData snap{};
        {
            std::scoped_lock lock(m_mutex);
            if (m_data.ResolutionWidth == width && m_data.ResolutionHeight == height) return;
            m_data.ResolutionWidth = width;
            m_data.ResolutionHeight = height;
            snap = m_data;
        }
        NotifyListeners(snap, SettingsDirtyFlags::Resolution);
    }

    void EngineSettings::SetTargetFramerate(i32 fps)
    {
        GraphicsSettingsData snap{};
        {
            std::scoped_lock lock(m_mutex);
            if (m_data.TargetFramerate == fps) return;
            m_data.TargetFramerate = fps;
            snap = m_data;
        }
        NotifyListeners(snap, SettingsDirtyFlags::TargetFramerate);
    }

    void EngineSettings::SetMsaaSamples(u8 samples)
    {
        GraphicsSettingsData snap{};
        {
            std::scoped_lock lock(m_mutex);
            if (m_data.MsaaSamples == samples) return;
            m_data.MsaaSamples = samples;
            snap = m_data;
        }
        NotifyListeners(snap, SettingsDirtyFlags::MSAA);
    }

    void EngineSettings::SetShadowQuality(ShadowQuality quality)
    {
        GraphicsSettingsData snap{};
        {
            std::scoped_lock lock(m_mutex);
            if (m_data.Shadows == quality) return;
            m_data.Shadows = quality;
            snap = m_data;
        }
        NotifyListeners(snap, SettingsDirtyFlags::ShadowQuality);
    }

    void EngineSettings::ApplyBatch(const GraphicsSettingsData& newSettings)
    {
        SettingsDirtyFlags flags = SettingsDirtyFlags::None;
        GraphicsSettingsData snap{};
        {
            std::scoped_lock lock(m_mutex);
            if (m_data.VSyncEnabled != newSettings.VSyncEnabled) flags = flags | SettingsDirtyFlags::VSync;
            if (m_data.Mode != newSettings.Mode)                 flags = flags | SettingsDirtyFlags::WindowMode;
            if (m_data.ResolutionWidth != newSettings.ResolutionWidth ||
                m_data.ResolutionHeight != newSettings.ResolutionHeight) flags = flags | SettingsDirtyFlags::Resolution;
            if (m_data.TargetFramerate != newSettings.TargetFramerate)   flags = flags | SettingsDirtyFlags::TargetFramerate;
            if (m_data.MsaaSamples != newSettings.MsaaSamples)           flags = flags | SettingsDirtyFlags::MSAA;
            if (m_data.Shadows != newSettings.Shadows)                   flags = flags | SettingsDirtyFlags::ShadowQuality;
            m_data = newSettings;
            snap = m_data;
        }
        if (flags != SettingsDirtyFlags::None)
        {
            NotifyListeners(snap, flags);
        }
    }

    bool EngineSettings::IsVSyncEnabled() const noexcept { std::scoped_lock lock(m_mutex); return m_data.VSyncEnabled; }
    WindowMode EngineSettings::GetWindowMode() const noexcept { std::scoped_lock lock(m_mutex); return m_data.Mode; }
    u32 EngineSettings::GetResolutionWidth() const noexcept { std::scoped_lock lock(m_mutex); return m_data.ResolutionWidth; }
    u32 EngineSettings::GetResolutionHeight() const noexcept { std::scoped_lock lock(m_mutex); return m_data.ResolutionHeight; }
    i32 EngineSettings::GetTargetFramerate() const noexcept { std::scoped_lock lock(m_mutex); return m_data.TargetFramerate; }
    u8 EngineSettings::GetMsaaSamples() const noexcept { std::scoped_lock lock(m_mutex); return m_data.MsaaSamples; }
    ShadowQuality EngineSettings::GetShadowQuality() const noexcept { std::scoped_lock lock(m_mutex); return m_data.Shadows; }

    u32 EngineSettings::RegisterListener(ChangeCallback callback)
    {
        std::scoped_lock lock(m_mutex);
        const u32 id = m_nextListenerId++;
        m_listeners.push_back(ListenerEntry{ .Id = id, .Callback = std::move(callback) });
        return id;
    }

    void EngineSettings::ClearListeners()
    {
        std::scoped_lock lock(m_mutex);
        m_listeners.clear();
    }

    void EngineSettings::NotifyListeners(const GraphicsSettingsData& snapshot, SettingsDirtyFlags flags)
    {
        std::vector<ListenerEntry> listenersCopy;
        {
            std::scoped_lock lock(m_mutex);
            listenersCopy = m_listeners;
        }
        for (const auto& entry : listenersCopy)
        {
            if (entry.Callback)
            {
                entry.Callback(snapshot, flags);
            }
        }
    }
}

MORBI_C_API void Morbi_Settings_ResetDefaults()
{
    Morbi::Core::EngineSettings::Global().ResetToDefaults();
}

MORBI_C_API void Morbi_Settings_GetSnapshot(Morbi::Core::GraphicsSettingsData* outSnapshot)
{
    if (outSnapshot)
    {
        *outSnapshot = Morbi::Core::EngineSettings::Global().Snapshot();
    }
}

MORBI_C_API void Morbi_Settings_SetVSync(bool enabled)
{
    Morbi::Core::EngineSettings::Global().SetVSync(enabled);
}

MORBI_C_API bool Morbi_Settings_GetVSync()
{
    return Morbi::Core::EngineSettings::Global().IsVSyncEnabled();
}

MORBI_C_API void Morbi_Settings_SetWindowMode(Morbi::u8 mode)
{
    Morbi::Core::EngineSettings::Global().SetWindowMode(static_cast<Morbi::Core::WindowMode>(mode));
}

MORBI_C_API Morbi::u8 Morbi_Settings_GetWindowMode()
{
    return static_cast<Morbi::u8>(Morbi::Core::EngineSettings::Global().GetWindowMode());
}

MORBI_C_API void Morbi_Settings_SetResolution(Morbi::u32 width, Morbi::u32 height)
{
    Morbi::Core::EngineSettings::Global().SetResolution(width, height);
}

MORBI_C_API void Morbi_Settings_SetTargetFramerate(Morbi::i32 fps)
{
    Morbi::Core::EngineSettings::Global().SetTargetFramerate(fps);
}

MORBI_C_API Morbi::i32 Morbi_Settings_GetTargetFramerate()
{
    return Morbi::Core::EngineSettings::Global().GetTargetFramerate();
}

MORBI_C_API void Morbi_Settings_SetMsaaSamples(Morbi::u8 samples)
{
    Morbi::Core::EngineSettings::Global().SetMsaaSamples(samples);
}

MORBI_C_API void Morbi_Settings_SetShadowQuality(Morbi::u8 quality)
{
    Morbi::Core::EngineSettings::Global().SetShadowQuality(static_cast<Morbi::Core::ShadowQuality>(quality));
}
