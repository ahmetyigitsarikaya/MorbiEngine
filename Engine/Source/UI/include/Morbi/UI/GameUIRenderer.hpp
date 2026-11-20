#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "Morbi/Core/Export.hpp"
#include "Morbi/Core/Math/Math.hpp"
#include "Morbi/Core/Types.hpp"
#include "Morbi/Platform/InputState.hpp"
#include "Morbi/RHI/IDevice.hpp"
#include "Morbi/Render/RenderGraph.hpp"

namespace Morbi::UI
{
    enum class GameUIWidgetKind : u8
    {
        HealthBar = 0,
        TelemetryBadge,
        Crosshair,
        MiniMenu,
        ScoreBadge,
        Custom
    };

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4324)
#endif
    struct GameUIWidgetNode
    {
        std::string Id;
        GameUIWidgetKind Kind = GameUIWidgetKind::Custom;
        std::string TextLabel;
        Math::Vec4 NormalizedRect{ 0.02f, 0.02f, 0.25f, 0.06f }; // x, y, width, height in [0..1]
        Math::Vec4 TintColor{ 0.12f, 0.78f, 0.96f, 0.90f };
        Math::Vec4 BackgroundColor{ 0.04f, 0.08f, 0.14f, 0.88f };
        f32 ProgressValue = 1.0f; // [0..1] for health/stamina/progress
        f32 Value = 100.0f;
        f32 MaxValue = 100.0f;
        bool Visible = true;
        bool Interactive = false;
        bool Focused = false;
        std::vector<std::string> MenuItems;
        i32 SelectedMenuItemIndex = 0;
    };
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

    struct GameUIRenderStats
    {
        u32 ActiveWidgetCount = 0;
        u32 OffscreenPassesRecorded = 0;
        u32 OffscreenWidth = 0;
        u32 OffscreenHeight = 0;
        u32 DataBindingEventsProcessed = 0;
        bool MenuVisible = false;
        i32 SelectedMenuItem = 0;
    };

    // Push constant structure (128 bytes = 32 DWORDs) matching GPU procedural HUD shader
    struct GameUIPushConstants
    {
        f32 HealthBarRect[4]{};
        f32 HealthBarColor[4]{};
        f32 TelemetryRect[4]{};
        f32 TelemetryColor[4]{};
        f32 CrosshairColor[4]{};
        f32 MenuRect[4]{};
        f32 MenuColor[4]{};
        f32 HealthProgress = 1.0f;
        f32 CrosshairSpread = 0.005f;
        u32 MenuVisible = 0u;
        u32 MenuSelectedIndex = 0u;
    };
    static_assert(sizeof(GameUIPushConstants) == 128, "GameUIPushConstants must be exactly 128 bytes.");

    // ============================================================================
    // MorbiGameUI - Lightweight In-Game Runtime HUD & Menu Renderer
    // ============================================================================
    // Evaluates the in-game HUD widget tree (Health, Crosshair, Telemetry, Menus),
    // handles real-time data-binding events from C# / C++, routes player input,
    // and records offscreen HUD passes into RenderGraph before alpha compositing.
    // ============================================================================
    class MORBI_API GameUIRenderer
    {
    public:
        GameUIRenderer();
        ~GameUIRenderer();

        GameUIRenderer(const GameUIRenderer&) = delete;
        GameUIRenderer& operator=(const GameUIRenderer&) = delete;

        bool Initialize(u32 width, u32 height);
        void InitializeSmokeTestDefaults();
        void ClearWidgets();
        void Shutdown();

        // Data-Driven Scene / Project Config Loading
        bool LoadFromSceneJson(const std::filesystem::path& scenePath);
        bool LoadFromJsonString(std::string_view jsonContent);

        void UpsertWidget(const GameUIWidgetNode& widget);
        [[nodiscard]] GameUIWidgetNode* FindWidget(std::string_view id);
        [[nodiscard]] const GameUIWidgetNode* FindWidget(std::string_view id) const;

        // Data-Binding API (invoked from C# scripts or C++ ECS)
        void SetDataProperty(std::string_view key, f32 value, std::string_view stringValue = {});
        void TriggerEvent(std::string_view eventName, i32 eventPayload);

        // Telemetry Update
        void UpdateHUDTelemetry(f32 fps, f32 playerAltitudeY, u32 activeEntities, u32 hotReloadGen);

        // 3D Viewport Transform Gizmo Parameters
        void SetGizmoParameters(f32 centerU, f32 centerV, f32 mode, bool isEditorMode, f32 aspect);

        // Input Routing & Focus System (routes keyboard/mouse inputs to active UI menus)
        bool ProcessInput(const Platform::InputState& input);
        [[nodiscard]] bool HasInputFocus() const noexcept;
        void ToggleMiniMenu(bool visible);

        // Adds the offscreen GameUI HUD pass to the RenderGraph and returns transient RGTextureHandle
        [[nodiscard]] Render::RGTextureHandle AddRenderGraphHUDPass(
            Render::RenderGraph& renderGraph,
            u32 width,
            u32 height,
            RHI::Format targetFormat,
            RHI::PipelineHandle hudPSO,
            u32 frameIndex);

        [[nodiscard]] GameUIPushConstants GetPushConstants() const noexcept;
        [[nodiscard]] GameUIRenderStats GetStats() const noexcept;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_impl;
    };
}
