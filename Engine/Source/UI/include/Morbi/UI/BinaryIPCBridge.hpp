#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "Morbi/Core/Export.hpp"
#include "Morbi/Core/Math/Math.hpp"
#include "Morbi/Core/Types.hpp"
#include "Morbi/Scene/Scene.hpp"

namespace Morbi::UI
{
    // ============================================================================
    // Binary IPC Protocol (C++ SoA ECS <-> JS/TS WebView2 TypedArray / SharedMemory)
    // ============================================================================
    // Eliminates JSON serialization & parsing overhead for high-frequency streams
    // (60+ Hz Transform gizmo dragging, camera orbit, telemetry, and entity selection).
    // ============================================================================

    inline constexpr u32 kBinaryIPCMagic = 0x4D554942u; // 'MUIB' (Morbi UI Binary)
    inline constexpr u16 kBinaryIPCVersion = 1u;

    enum class BinaryIPCOpcode : u16
    {
        Nop                     = 0,
        SyncEntityTransform     = 1, // 48-byte payload (EntityId + Pos[3] + RotQuat[4] + Scale[3])
        ViewportResize          = 2, // 24-byte payload (Width, Height, DpiScale, SharedTextureHandle)
        CameraOrbitInput        = 3, // 16-byte payload (DeltaYaw, DeltaPitch, DeltaZoom, Buttons)
        EngineTelemetrySnapshot = 4, // 32-byte payload (FrameTimeMs, Fps, Entities, Physics, HotReload, DrawCalls)
        SelectEntity            = 5, // 8-byte payload  (PackedEntityId)
        ExecuteCommand          = 6, // UTF-8 string command payload
        ViewportPick            = 7  // 8-byte payload  (NormalizedX float, NormalizedY float)
    };

#pragma pack(push, 1)
    // 16-byte aligned binary packet header directly readable via JS `DataView`
    struct BinaryPacketHeader
    {
        u32 Magic = kBinaryIPCMagic;
        u16 Opcode = static_cast<u16>(BinaryIPCOpcode::Nop);
        u16 Version = kBinaryIPCVersion;
        u32 SequenceId = 0;
        u32 PayloadSizeBytes = 0;
    };
    static_assert(sizeof(BinaryPacketHeader) == 16, "BinaryPacketHeader must be exactly 16 bytes for JS DataView.");

    // 48-byte payload mapped directly to JS `Float32Array` + `BigUint64Array`
    struct BinaryTransformPayload
    {
        u64 PackedEntityId = 0;
        f32 Position[3]{ 0.0f, 0.0f, 0.0f };
        f32 RotationQuat[4]{ 0.0f, 0.0f, 0.0f, 1.0f };
        f32 Scale[3]{ 1.0f, 1.0f, 1.0f };
    };
    static_assert(sizeof(BinaryTransformPayload) == 48, "BinaryTransformPayload must be 48 bytes.");

    // 24-byte payload for Shared Texture & Viewport composition resizing
    struct BinaryViewportResizePayload
    {
        u32 Width = 1280;
        u32 Height = 720;
        f32 DpiScale = 1.0f;
        u32 Reserved = 0;
        u64 SharedTextureHandle = 0;
    };
    static_assert(sizeof(BinaryViewportResizePayload) == 24, "BinaryViewportResizePayload must be 24 bytes.");

    // 32-byte payload for live 60+ Hz Engine & RenderGraph telemetry
    struct BinaryTelemetryPayload
    {
        f32 FrameTimeMs = 0.0f;
        f32 Fps = 0.0f;
        u32 AliveEntities = 0;
        u32 PhysicsActiveBodies = 0;
        u32 HotReloadGeneration = 0;
        u32 RenderGraphPasses = 0;
        u32 AliasedTexturesReused = 0;
        u32 AudioVoicesPlayed = 0;
    };
    static_assert(sizeof(BinaryTelemetryPayload) == 32, "BinaryTelemetryPayload must be 32 bytes.");

    // 8-byte payload for 3D Viewport mouse raycast / GPU picking
    struct BinaryViewportPickPayload
    {
        f32 NormalizedX = 0.0f;
        f32 NormalizedY = 0.0f;
    };
    static_assert(sizeof(BinaryViewportPickPayload) == 8, "BinaryViewportPickPayload must be 8 bytes.");
#pragma pack(pop)

    struct BinaryIPCStats
    {
        u64 PacketsSentToJS = 0;
        u64 BytesSentToJS = 0;
        u64 PacketsReceivedFromJS = 0;
        u64 BytesReceivedFromJS = 0;
        u64 ZeroCopyTransformMutations = 0;
        u64 ViewportPicksProcessed = 0;
        u64 EntitySelectionsProcessed = 0;
        bool SharedMemoryMapped = false;
    };

    class MORBI_API BinaryIPCBridge
    {
    public:
        using PacketCallback = std::function<void(const BinaryPacketHeader& header, std::span<const u8> payload)>;

        BinaryIPCBridge();
        ~BinaryIPCBridge();

        BinaryIPCBridge(const BinaryIPCBridge&) = delete;
        BinaryIPCBridge& operator=(const BinaryIPCBridge&) = delete;

        bool Initialize(
            std::string_view sharedMemoryName = "Local\\MorbiEngine_UI_SharedArena",
            usize ringCapacityBytes = 512ull * 1024ull);
        void Shutdown();

        // Outbound C++ -> JS/TS Binary Ring Buffer Writers
        bool PushPacket(BinaryIPCOpcode opcode, std::span<const u8> payload);
        bool PushTransformUpdate(Scene::EntityId entity, const Scene::TransformComponent& transform);
        bool PushTelemetry(const BinaryTelemetryPayload& telemetry);
        bool PushViewportSharedTexture(u32 width, u32 height, f32 dpiScale, u64 sharedTextureHandle);
        bool PushEntitySelected(Scene::EntityId entity);

        // Inbound JS/TS -> C++ Binary Ring Buffer Readers (e.g. WebView2 PostWebMessageAsBinary / SharedArrayBuffer)
        bool EnqueueIncomingBinaryFrame(std::span<const u8> rawFrameBytes);
        bool SimulateJSTransformDrag(Scene::EntityId entity, const Math::Vec3& newPosition);
        bool SimulateJSViewportPick(f32 normX, f32 normY);
        usize PollIncomingPackets(const PacketCallback& callback);

        // Applies any pending `SyncEntityTransform` packets from JS/React directly into the SoA TransformComponent pool
        u32 ApplyIncomingTransformMutations(Scene::Scene& scene);

        // Selected entity tracking from Viewport picking or UI Hierarchy selection
        void SetSelectedEntity(std::optional<Scene::EntityId> entity) noexcept;
        [[nodiscard]] std::optional<Scene::EntityId> GetLastSelectedEntity() const noexcept;
        [[nodiscard]] bool ConsumePendingViewportPick(f32& outNormX, f32& outNormY) noexcept;
        [[nodiscard]] bool ConsumePendingCommand(std::string& outCommand) noexcept;

        // Exports the current outbound binary batch for WebView2 `PostWebMessageAsBinary` or Shared Memory view
        [[nodiscard]] std::span<const u8> GetOutboundBatchBytes() const noexcept;
        void ClearOutboundBatch() noexcept;

        [[nodiscard]] BinaryIPCStats GetStats() const noexcept;
        [[nodiscard]] void* GetSharedMemoryPointer() const noexcept;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_impl;
    };
}
