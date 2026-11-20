#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

#include "Morbi/Core/Export.hpp"
#include "Morbi/Core/Result.hpp"
#include "Morbi/Core/Types.hpp"
#include "Morbi/Platform/Window.hpp"
#include "Morbi/RHI/IDevice.hpp"
#include "Morbi/Scene/Scene.hpp"
#include "Morbi/UI/BinaryIPCBridge.hpp"

namespace Morbi::UI
{
    enum class UIHostSurfaceMode : u8
    {
        EditorAndHub = 0, // React + TS + Tailwind Editor & Hub (Shared Texture 3D Viewport + Panels)
        GameHUDOnly  = 1  // Pure Runtime Game UI
    };

    struct ViewportRect
    {
        i32 X = 260;
        i32 Y = 42;
        u32 Width = 760;
        u32 Height = 500;
    };

    struct WebView2CompositorDesc
    {
        UIHostSurfaceMode Mode = UIHostSurfaceMode::EditorAndHub;
        std::filesystem::path FrontendRootDir;
        std::string DevServerUrl = "http://localhost:5173";
        u32 InitialWidth = 1280;
        u32 InitialHeight = 720;
        bool HeadlessMode = false;
    };

    struct WebView2CompositorStats
    {
        bool DirectCompositionActive = false;
        bool SharedViewportTextureActive = false;
        u32 SharedTextureBindlessIndex = 0xFFFFFFFFu;
        u64 SharedTextureGpuHandle = 0;
        u64 ComposedFrames = 0;
        u64 BinaryIPCPacketsFlushed = 0;
    };

    enum class EditorPlayMode : u8
    {
        Edit = 0,    // Stopped / Normal Editor Mode (Default on launch)
        Playing = 1, // Active PIE simulation
        Paused = 2   // Paused PIE simulation (frame-by-frame stepping)
    };

    struct AssetInstantiationRequest
    {
        std::string AssetPath;
        std::string AssetName;
        f32 NormX = 0.5f;
        f32 NormY = 0.5f;
    };

    enum class EntityActionKind : u8
    {
        None = 0,
        Create,
        Delete,
        Rename,
        AddComponent,
        Duplicate
    };

    struct EntityCommandRequest
    {
        EntityActionKind Action = EntityActionKind::None;
        u32 EntityId = 0;
        std::string Name;
        std::string TargetType;
    };

    struct PhysicsTelemetrySnapshot
    {
        u32 ActiveBodies = 0;
        u32 SleepingBodies = 0;
        u32 DynamicBodies = 0;
        u32 KinematicBodies = 0;
        u32 StaticBodies = 0;
        u32 TotalBodies = 0;
        u32 ActiveJoints = 0;
        u32 Characters = 0;
        u32 CollisionPairs = 0;
        u32 TriggerPairs = 0;
        u32 Substeps = 0;
        u32 Queries = 0;
        f32 SimulationTimeMs = 0.0f;
    };

    // ============================================================================
    // WebView2 Visual Composition & Zero-Copy Shared GPU Texture Controller
    // ============================================================================
    // Eliminates the Win32 HWND "airspace" z-order conflict by compositing the
    // Vulkan 1.3 / DirectX 12 3D Viewport texture and the WebView2 UI visual tree
    // inside a unified DirectComposition / Shared Texture pipeline.
    // ============================================================================
    class MORBI_API WebView2Compositor
    {
    public:
        WebView2Compositor();
        ~WebView2Compositor();

        WebView2Compositor(const WebView2Compositor&) = delete;
        WebView2Compositor& operator=(const WebView2Compositor&) = delete;

        [[nodiscard]] Core::Result<bool, std::string> Initialize(
            const WebView2CompositorDesc& desc,
            const Platform::NativeWindowHandle& windowHandle,
            RHI::IDevice& rhiDevice,
            BinaryIPCBridge& ipcBridge);

        void Shutdown(RHI::IDevice& rhiDevice);

        // Sets the native 3D Viewport HWND to allow dynamic clipping when menus/dialogs overlap
        void SetViewportChildWindow(void* hwnd);

        // Updates WebView2 controller bounds on window resize / maximize
        void OnWindowResize(u32 width, u32 height);

        // Resizes the shared 3D Viewport GPU texture and notifies React/TS over Binary IPC
        void ResizeViewport(RHI::IDevice& rhiDevice, const ViewportRect& newViewportRect, f32 dpiScale = 1.0f);

        // Pumps incoming JS/React Binary IPC packets (e.g., Transform gizmo drag) and flushes outbound telemetry
        void BeginFrameSync(
            Scene::Scene& activeScene,
            Scene::EntityId trackedEntity,
            const BinaryTelemetryPayload& telemetry);

        // Commits the DirectComposition visual tree & shared viewport texture after RenderGraph execution
        void EndFrameComposite();

        // Live bidirectional data sync with the React/TS Frontend
        bool ProcessFrontendJsonMessage(std::string_view jsonPayload, Scene::Scene& activeScene);
        void PostJsonToFrontend(std::string_view jsonPayload);
        void SyncSceneData(const Scene::Scene& scene, std::string_view sceneName);
        [[nodiscard]] const std::string& GetCachedSceneSyncJson() const noexcept;
        void SyncAssetData(const std::filesystem::path& assetsDir);
        void PushLogMessage(std::string_view level, std::string_view category, std::string_view message, std::string_view timestamp);

        // PIE (Play-in-Editor) Transport State Management
        [[nodiscard]] EditorPlayMode GetPlayMode() const noexcept;
        void SetPlayMode(EditorPlayMode mode) noexcept;
        [[nodiscard]] bool ConsumePlayModeTransition(EditorPlayMode& outRequestedMode) noexcept;

        [[nodiscard]] bool IsSimulationPaused() const noexcept;
        void SetSimulationPaused(bool paused) noexcept;
        [[nodiscard]] bool ConsumeStepRequested() noexcept;

        // Drag & Drop Asset Instantiation from Asset Browser
        [[nodiscard]] bool ConsumeAssetInstantiation(AssetInstantiationRequest& outReq) noexcept;

        // Entity ECS Operations (Create, Delete, Rename, AddComponent)
        [[nodiscard]] bool ConsumeEntityCommand(EntityCommandRequest& outReq) noexcept;

        // Scene Lifecycle & Serialization (New, Open/Template Load, Save, Save As)
        [[nodiscard]] bool ConsumeNewSceneRequest() noexcept;
        [[nodiscard]] bool ConsumeLoadSceneRequest(std::string& outPath) noexcept;
        [[nodiscard]] bool ConsumeSaveSceneRequest(std::string& outPath) noexcept;
        void TriggerOpenSceneDialog() noexcept;
        void TriggerSaveSceneAsDialog() noexcept;

        // External 3D Model Import & File Dialog
        [[nodiscard]] bool ConsumeImportModelRequest(std::string& outPath) noexcept;
        void QueueExternalModelImport(std::string_view path);
        void TriggerImportModelDialog() noexcept;

        // Project Build & Live C# Script Recompilation
        [[nodiscard]] bool ConsumeCompileScriptsRequest() noexcept;
        [[nodiscard]] bool ConsumeBuildGameRequest() noexcept;

        // Active Gizmo Tool Mode (translate, rotate, scale, select)
        [[nodiscard]] std::string GetActiveGizmoTool() const noexcept;

        // Active Viewport Tab (Scene, Game, Asset Browser, etc.)
        [[nodiscard]] std::string GetActiveViewportTab() const noexcept;

        // Physics Telemetry, Inspector Sync & Debug Draw
        void SetPhysicsTelemetry(const PhysicsTelemetrySnapshot& stats) noexcept;
        [[nodiscard]] bool ConsumePhysicsSyncEntity(Scene::EntityId& outEntity) noexcept;
        [[nodiscard]] bool IsPhysicsDebugDrawEnabled() const noexcept;
        void SetPhysicsDebugDrawEnabled(bool enabled) noexcept;

        [[nodiscard]] RHI::TextureHandle GetSharedViewportTexture() const noexcept;
        [[nodiscard]] RHI::BindlessResourceIndex GetSharedViewportBindlessIndex() const noexcept;
        [[nodiscard]] ViewportRect GetViewportRect() const noexcept;
        [[nodiscard]] WebView2CompositorStats GetStats() const noexcept;

        struct Impl;
    private:
        std::unique_ptr<Impl> m_impl;
    };
}
