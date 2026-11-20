#include "Morbi/UI/GameUIRenderer.hpp"

#include <algorithm>
#include <fstream>
#include <nlohmann/json.hpp>

#include "Morbi/Core/Assert.hpp"
#include "Morbi/Core/Log.hpp"

namespace Morbi::UI
{
    struct GameUIRenderer::Impl
    {
        std::vector<GameUIWidgetNode> Widgets;
        GameUIRenderStats Stats{};
        bool Initialized = false;
        bool MiniMenuVisible = false;
        i32 SelectedMenuItem = 0;
        std::vector<std::string> MenuItems = {
            "RESUME MISSION",
            "RELOAD WEAPONS",
            "GRAPHICS SETTINGS",
            "EXIT TO DESKTOP"
        };
        f32 PlayerHealth = 100.0f;
        f32 PlayerSpeed = 0.0f;
        u32 PlayerScore = 0;
        f32 CrosshairSpread = 0.005f;
        f32 GizmoCenterU = 0.5f;
        f32 GizmoCenterV = 0.5f;
        f32 GizmoMode = 1.0f;
        bool GizmoEditorMode = false;
        f32 GizmoAspect = 16.0f / 9.0f;
    };

    GameUIRenderer::GameUIRenderer()
        : m_impl(std::make_unique<Impl>())
    {
    }

    GameUIRenderer::~GameUIRenderer()
    {
        Shutdown();
    }

    bool GameUIRenderer::Initialize(u32 width, u32 height)
    {
        ClearWidgets();
        m_impl->Stats.OffscreenWidth = width;
        m_impl->Stats.OffscreenHeight = height;
        m_impl->Initialized = true;
        Core::Log::Info("UI.GameHUD", "Initialized Runtime GameUI HUD Renderer (0 default test widgets, Offscreen RenderGraph target " +
            std::to_string(width) + "x" + std::to_string(height) + ")");
        return true;
    }

    void GameUIRenderer::InitializeSmokeTestDefaults()
    {
        if (!m_impl)
        {
            return;
        }
        m_impl->Widgets.clear();

        UpsertWidget(GameUIWidgetNode{
            .Id = "hud.health_bar",
            .Kind = GameUIWidgetKind::HealthBar,
            .TextLabel = "HEALTH INTEGRITY: 100 / 100 HP",
            .NormalizedRect = { 0.03f, 0.03f, 0.22f, 0.035f },
            .TintColor = { 0.15f, 0.88f, 0.52f, 0.95f },
            .BackgroundColor = { 0.04f, 0.08f, 0.14f, 0.88f },
            .ProgressValue = 1.0f,
            .Value = 100.0f,
            .MaxValue = 100.0f,
            .Visible = true,
            .Interactive = false
        });

        UpsertWidget(GameUIWidgetNode{
            .Id = "hud.telemetry_badge",
            .Kind = GameUIWidgetKind::TelemetryBadge,
            .TextLabel = "FPS: 60 | SPEED: 0.0 m/s | ENTITIES: 0",
            .NormalizedRect = { 0.03f, 0.075f, 0.30f, 0.04f },
            .TintColor = { 0.10f, 0.45f, 0.85f, 0.85f },
            .BackgroundColor = { 0.04f, 0.08f, 0.14f, 0.85f },
            .ProgressValue = 1.0f,
            .Visible = true,
            .Interactive = false
        });

        UpsertWidget(GameUIWidgetNode{
            .Id = "hud.crosshair",
            .Kind = GameUIWidgetKind::Crosshair,
            .TextLabel = "+",
            .NormalizedRect = { 0.495f, 0.495f, 0.01f, 0.01f },
            .TintColor = { 0.95f, 0.98f, 1.00f, 0.90f },
            .ProgressValue = 1.0f,
            .Visible = true,
            .Interactive = false
        });

        UpsertWidget(GameUIWidgetNode{
            .Id = "hud.mini_menu",
            .Kind = GameUIWidgetKind::MiniMenu,
            .TextLabel = "TACTICAL OVERLAY MENU",
            .NormalizedRect = { 0.35f, 0.25f, 0.30f, 0.50f },
            .TintColor = { 0.06f, 0.09f, 0.15f, 0.92f },
            .BackgroundColor = { 0.03f, 0.05f, 0.10f, 0.95f },
            .ProgressValue = 1.0f,
            .Visible = false,
            .Interactive = true,
            .Focused = false,
            .MenuItems = m_impl->MenuItems,
            .SelectedMenuItemIndex = 0
        });
    }

    void GameUIRenderer::ClearWidgets()
    {
        if (!m_impl)
        {
            return;
        }
        m_impl->Widgets.clear();
        m_impl->MiniMenuVisible = false;
        m_impl->Stats.MenuVisible = false;
        m_impl->Stats.ActiveWidgetCount = 0;
    }

    void GameUIRenderer::Shutdown()
    {
        if (!m_impl)
        {
            return;
        }
        m_impl->Widgets.clear();
        m_impl->Initialized = false;
    }

    bool GameUIRenderer::LoadFromSceneJson(const std::filesystem::path& scenePath)
    {
        if (!std::filesystem::exists(scenePath))
        {
            return false;
        }

        std::ifstream in(scenePath);
        if (!in.is_open())
        {
            return false;
        }

        try
        {
            nlohmann::json j;
            in >> j;
            if (j.contains("hud") && j["hud"].is_object())
            {
                const auto& hj = j["hud"];
                if (hj.contains("widgets") && hj["widgets"].is_array())
                {
                    for (const auto& wj : hj["widgets"])
                    {
                        GameUIWidgetNode node{};
                        node.Id = wj.value("id", "hud.widget");
                        const std::string typeStr = wj.value("type", "Custom");
                        if (typeStr == "HealthBar") node.Kind = GameUIWidgetKind::HealthBar;
                        else if (typeStr == "TelemetryBadge") node.Kind = GameUIWidgetKind::TelemetryBadge;
                        else if (typeStr == "Crosshair") node.Kind = GameUIWidgetKind::Crosshair;
                        else if (typeStr == "MiniMenu") node.Kind = GameUIWidgetKind::MiniMenu;
                        else if (typeStr == "ScoreBadge") node.Kind = GameUIWidgetKind::ScoreBadge;

                        node.TextLabel = wj.value("label", "");
                        if (wj.contains("position") && wj["position"].size() == 4)
                        {
                            node.NormalizedRect = {
                                wj["position"][0].get<f32>(),
                                wj["position"][1].get<f32>(),
                                wj["position"][2].get<f32>(),
                                wj["position"][3].get<f32>()
                            };
                        }
                        if (wj.contains("color") && wj["color"].size() >= 3)
                        {
                            node.TintColor = {
                                wj["color"][0].get<f32>(),
                                wj["color"][1].get<f32>(),
                                wj["color"][2].get<f32>(),
                                wj["color"].size() > 3 ? wj["color"][3].get<f32>() : 1.0f
                            };
                        }
                        node.Value = wj.value("value", 100.0f);
                        node.MaxValue = wj.value("max_value", 100.0f);
                        node.ProgressValue = (node.MaxValue > 0.0f) ? std::clamp(node.Value / node.MaxValue, 0.0f, 1.0f) : 1.0f;
                        node.Visible = wj.value("visible", true);

                        if (wj.contains("items") && wj["items"].is_array())
                        {
                            node.MenuItems.clear();
                            for (const auto& item : wj["items"])
                            {
                                node.MenuItems.push_back(item.get<std::string>());
                            }
                            m_impl->MenuItems = node.MenuItems;
                        }

                        UpsertWidget(node);
                    }
                }
                Core::Log::Info("UI.GameHUD", "Loaded Scene HUD Configuration from VFS: " +
                    std::to_string(m_impl->Widgets.size()) + " widgets loaded from '" + scenePath.string() + "'");
                return true;
            }
        }
        catch (const std::exception& ex)
        {
            Core::Log::Warn("UI.GameHUD", "Failed to parse HUD config from scene JSON: " + std::string(ex.what()));
        }
        return false;
    }

    bool GameUIRenderer::LoadFromJsonString(std::string_view jsonContent)
    {
        try
        {
            const auto j = nlohmann::json::parse(jsonContent);
            if (j.contains("hud"))
            {
                // Can re-use logic
                return true;
            }
        }
        catch (...)
        {
            return false;
        }
        return false;
    }

    void GameUIRenderer::UpsertWidget(const GameUIWidgetNode& widget)
    {
        for (auto& existing : m_impl->Widgets)
        {
            if (existing.Id == widget.Id)
            {
                existing = widget;
                return;
            }
        }
        m_impl->Widgets.push_back(widget);
        m_impl->Stats.ActiveWidgetCount = static_cast<u32>(m_impl->Widgets.size());
    }

    GameUIWidgetNode* GameUIRenderer::FindWidget(std::string_view id)
    {
        for (auto& w : m_impl->Widgets)
        {
            if (w.Id == id)
            {
                return &w;
            }
        }
        return nullptr;
    }

    const GameUIWidgetNode* GameUIRenderer::FindWidget(std::string_view id) const
    {
        for (const auto& w : m_impl->Widgets)
        {
            if (w.Id == id)
            {
                return &w;
            }
        }
        return nullptr;
    }

    void GameUIRenderer::SetDataProperty(std::string_view key, f32 value, std::string_view stringValue)
    {
        ++m_impl->Stats.DataBindingEventsProcessed;

        if (key == "player.health")
        {
            m_impl->PlayerHealth = value;
            if (auto* w = FindWidget("hud.health_bar"))
            {
                w->Value = value;
                w->ProgressValue = std::clamp(value / (w->MaxValue > 0.0f ? w->MaxValue : 100.0f), 0.0f, 1.0f);
                if (!stringValue.empty())
                {
                    w->TextLabel = std::string(stringValue);
                }
                // Tint to amber/red if health is low
                if (w->ProgressValue < 0.25f)
                {
                    w->TintColor = { 0.95f, 0.20f, 0.15f, 0.95f };
                }
                else if (w->ProgressValue < 0.50f)
                {
                    w->TintColor = { 0.95f, 0.75f, 0.15f, 0.95f };
                }
                else
                {
                    w->TintColor = { 0.15f, 0.88f, 0.52f, 0.95f };
                }
            }
        }
        else if (key == "player.speed")
        {
            m_impl->PlayerSpeed = value;
            if (auto* w = FindWidget("hud.telemetry_badge"))
            {
                if (!stringValue.empty())
                {
                    w->TextLabel = std::string(stringValue);
                }
            }
        }
        else if (key == "player.score")
        {
            m_impl->PlayerScore = static_cast<u32>(value);
        }
        else if (key == "crosshair.spread")
        {
            m_impl->CrosshairSpread = std::clamp(value, 0.002f, 0.040f);
        }
    }

    void GameUIRenderer::TriggerEvent(std::string_view eventName, i32 eventPayload)
    {
        ++m_impl->Stats.DataBindingEventsProcessed;

        if (eventName == "player.jump")
        {
            // Momentarily widen crosshair
            m_impl->CrosshairSpread = 0.012f;
        }
        else if (eventName == "menu.select")
        {
            const usize selIdx = static_cast<usize>(eventPayload);
            if (selIdx < m_impl->MenuItems.size())
            {
                Core::Log::Info("UI.GameHUD", "[InputRouting] Menu Selected: '" +
                    m_impl->MenuItems[selIdx] + "' (Index=" + std::to_string(selIdx) + ")");
            }
        }
        else if (eventName == "menu.toggle")
        {
            ToggleMiniMenu(eventPayload != 0);
        }
    }

    void GameUIRenderer::UpdateHUDTelemetry(f32 fps, f32 playerAltitudeY, u32 activeEntities, u32 hotReloadGen)
    {
        if (auto* w = FindWidget("hud.telemetry_badge"))
        {
            w->TextLabel = "FPS: " + std::to_string(static_cast<u32>(fps)) +
                " | Alt: " + std::to_string(static_cast<i32>(playerAltitudeY * 10.0f) / 10.0f) + "m" +
                " | Ent: " + std::to_string(activeEntities) +
                " | HotReload: Gen " + std::to_string(hotReloadGen);
            w->ProgressValue = std::clamp(playerAltitudeY / 5.0f, 0.1f, 1.0f);
        }
    }

    void GameUIRenderer::SetGizmoParameters(f32 centerU, f32 centerV, f32 mode, bool isEditorMode, f32 aspect)
    {
        if (!m_impl) return;
        m_impl->GizmoCenterU = centerU;
        m_impl->GizmoCenterV = centerV;
        m_impl->GizmoMode = mode;
        m_impl->GizmoEditorMode = isEditorMode;
        m_impl->GizmoAspect = aspect;
    }

    bool GameUIRenderer::ProcessInput(const Platform::InputState& input)
    {
        if (!FindWidget("hud.mini_menu"))
        {
            return false;
        }

        // Scancode 41 = Escape: toggle in-game Tactical Mini-Menu
        if (input.WasKeyPressedThisFrame(41))
        {
            ToggleMiniMenu(!m_impl->MiniMenuVisible);
            return true;
        }

        // When menu is active, route keyboard and mouse navigation
        if (m_impl->MiniMenuVisible)
        {
            const i32 itemCount = static_cast<i32>(m_impl->MenuItems.size());
            if (itemCount > 0)
            {
                // Scancode 82 = Up Arrow, Scancode 26 = 'W'
                if (input.WasKeyPressedThisFrame(82) || input.WasKeyPressedThisFrame(26))
                {
                    m_impl->SelectedMenuItem = (m_impl->SelectedMenuItem + itemCount - 1) % itemCount;
                    m_impl->Stats.SelectedMenuItem = m_impl->SelectedMenuItem;
                    Core::Log::Info("UI.GameHUD", "[InputRouting] Menu Navigate UP -> " +
                        m_impl->MenuItems[static_cast<usize>(m_impl->SelectedMenuItem)]);
                    return true;
                }

                // Scancode 81 = Down Arrow, Scancode 22 = 'S'
                if (input.WasKeyPressedThisFrame(81) || input.WasKeyPressedThisFrame(22))
                {
                    m_impl->SelectedMenuItem = (m_impl->SelectedMenuItem + 1) % itemCount;
                    m_impl->Stats.SelectedMenuItem = m_impl->SelectedMenuItem;
                    Core::Log::Info("UI.GameHUD", "[InputRouting] Menu Navigate DOWN -> " +
                        m_impl->MenuItems[static_cast<usize>(m_impl->SelectedMenuItem)]);
                    return true;
                }

                // Scancode 40 = Enter / Return, Scancode 44 = Space
                if (input.WasKeyPressedThisFrame(40) || input.WasKeyPressedThisFrame(44) ||
                    input.WasMouseButtonPressedThisFrame(Platform::MouseButton::Left))
                {
                    TriggerEvent("menu.select", m_impl->SelectedMenuItem);
                    return true;
                }
            }
            return true; // Consume input while menu is focused
        }

        return false;
    }

    bool GameUIRenderer::HasInputFocus() const noexcept
    {
        return m_impl ? m_impl->MiniMenuVisible : false;
    }

    void GameUIRenderer::ToggleMiniMenu(bool visible)
    {
        m_impl->MiniMenuVisible = visible;
        m_impl->Stats.MenuVisible = visible;
        if (auto* w = FindWidget("hud.mini_menu"))
        {
            w->Visible = visible;
            w->Focused = visible;
        }
        Core::Log::Info("UI.GameHUD", "[InputRouting] Mini-Menu Toggled: " +
            std::string(visible ? "OPEN (Input Focus Acquired)" : "CLOSED (Input Focus Released)"));
    }

    GameUIPushConstants GameUIRenderer::GetPushConstants() const noexcept
    {
        GameUIPushConstants pc{};
        if (!m_impl)
        {
            return pc;
        }

        // In Editor Edit Mode: Only render the 3D In-Engine Transform Gizmo, suppress HUD bars
        if (m_impl->GizmoEditorMode)
        {
            pc.MenuVisible = 0u;
            pc.MenuRect[0] = m_impl->GizmoCenterU;
            pc.MenuRect[1] = m_impl->GizmoCenterV;
            pc.MenuRect[2] = m_impl->GizmoMode;
            pc.MenuRect[3] = 0.0f;
            pc.MenuColor[0] = 0.12f;
            pc.MenuColor[1] = 1.0f;  // Flag: IsEditorMode
            pc.MenuColor[2] = m_impl->GizmoAspect;
            pc.MenuColor[3] = (m_impl->GizmoMode > 0.0f) ? 1.0f : 0.0f;  // Flag: GizmoVisible
            return pc;
        }

        if (const auto* w = FindWidget("hud.health_bar"); w && w->Visible)
        {
            pc.HealthBarRect[0] = w->NormalizedRect.x;
            pc.HealthBarRect[1] = w->NormalizedRect.y;
            pc.HealthBarRect[2] = w->NormalizedRect.z;
            pc.HealthBarRect[3] = w->NormalizedRect.w;
            pc.HealthBarColor[0] = w->TintColor.x;
            pc.HealthBarColor[1] = w->TintColor.y;
            pc.HealthBarColor[2] = w->TintColor.z;
            pc.HealthBarColor[3] = w->TintColor.w;
            pc.HealthProgress = w->ProgressValue;
        }

        if (const auto* w = FindWidget("hud.telemetry_badge"); w && w->Visible)
        {
            pc.TelemetryRect[0] = w->NormalizedRect.x;
            pc.TelemetryRect[1] = w->NormalizedRect.y;
            pc.TelemetryRect[2] = w->NormalizedRect.z;
            pc.TelemetryRect[3] = w->NormalizedRect.w;
            pc.TelemetryColor[0] = w->TintColor.x;
            pc.TelemetryColor[1] = w->TintColor.y;
            pc.TelemetryColor[2] = w->TintColor.z;
            pc.TelemetryColor[3] = w->TintColor.w;
        }

        if (const auto* w = FindWidget("hud.crosshair"); w && w->Visible)
        {
            pc.CrosshairColor[0] = w->TintColor.x;
            pc.CrosshairColor[1] = w->TintColor.y;
            pc.CrosshairColor[2] = w->TintColor.z;
            pc.CrosshairColor[3] = w->TintColor.w;
            pc.CrosshairSpread = m_impl->CrosshairSpread;
        }

        if (const auto* w = FindWidget("hud.mini_menu"))
        {
            pc.MenuRect[0] = w->NormalizedRect.x;
            pc.MenuRect[1] = w->NormalizedRect.y;
            pc.MenuRect[2] = w->NormalizedRect.z;
            pc.MenuRect[3] = w->NormalizedRect.w;
            pc.MenuColor[0] = w->TintColor.x;
            pc.MenuColor[1] = w->TintColor.y;
            pc.MenuColor[2] = w->TintColor.z;
            pc.MenuColor[3] = w->TintColor.w;
            pc.MenuVisible = m_impl->MiniMenuVisible ? 1u : 0u;
            pc.MenuSelectedIndex = static_cast<u32>(std::max(0, m_impl->SelectedMenuItem));
        }

        return pc;
    }

    Render::RGTextureHandle GameUIRenderer::AddRenderGraphHUDPass(
        Render::RenderGraph& renderGraph,
        u32 width,
        u32 height,
        RHI::Format targetFormat,
        RHI::PipelineHandle hudPSO,
        u32 /*frameIndex*/)
    {
        m_impl->Stats.OffscreenWidth = width;
        m_impl->Stats.OffscreenHeight = height;

        const RHI::TextureDesc hudDesc{
            .Width = width,
            .Height = height,
            .PixelFormat = targetFormat,
            .RenderTarget = true,
            .Storage = false,
            .DebugName = "GameUI_OffscreenHUDTexture"
        };

        const Render::RGTextureHandle rgHudTex = renderGraph.CreateTransientTexture("GameUI_OffscreenHUD", hudDesc);

        renderGraph.AddPass("GameUI_OffscreenHUDPass",
            [&](Render::RenderGraphPassBuilder& builder) {
                builder.WriteTexture(rgHudTex, RHI::ResourceState::RenderTarget);
            },
            [this, rgHudTex, hudPSO](
                const Render::RenderGraphRegistry& reg,
                RHI::ICommandList& cmd) {
                RHI::RenderAttachmentDesc att{
                    .Texture = reg.GetTexture(rgHudTex),
                    .ClearOnLoad = true,
                    .ClearColor = { 0.0f, 0.0f, 0.0f, 0.0f } // Zero alpha background for transparent overlay
                };
                RHI::DynamicRenderPassDesc pass{
                    .ColorAttachments = std::span<const RHI::RenderAttachmentDesc>(&att, 1)
                };
                cmd.BeginRendering(pass);
                if (hudPSO.IsValid())
                {
                    cmd.SetPipeline(hudPSO);
                    const GameUIPushConstants push = GetPushConstants();
                    cmd.PushConstants(&push, sizeof(push), 0);
                    cmd.Draw(3, 1, 0, 0); // Fullscreen procedural HUD triangle
                }
                cmd.EndRendering();
                ++m_impl->Stats.OffscreenPassesRecorded;
            });

        return rgHudTex;
    }

    GameUIRenderStats GameUIRenderer::GetStats() const noexcept
    {
        return m_impl ? m_impl->Stats : GameUIRenderStats{};
    }
}
