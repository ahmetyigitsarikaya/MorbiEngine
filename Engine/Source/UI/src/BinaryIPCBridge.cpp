#include "Morbi/UI/BinaryIPCBridge.hpp"

#include <cstring>
#include <mutex>

#include "Morbi/Core/Diagnostics/BreadcrumbTracker.h"
#include "Morbi/Core/Log.hpp"

#if defined(_WIN32)
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <Windows.h>
#endif

namespace Morbi::UI
{
    namespace
    {
#pragma pack(push, 1)
        struct SharedMemoryControlBlock
        {
            u32 Magic = kBinaryIPCMagic;
            u32 CapacityBytes = 0;
            u32 WriteCursor = 0;
            u32 SequenceCounter = 0;
        };
#pragma pack(pop)
    }

    struct BinaryIPCBridge::Impl
    {
        std::string SharedMemoryName;
        usize CapacityBytes = 0;
        void* FileMappingHandle = nullptr;
        u8* MappedBasePtr = nullptr;

        mutable std::mutex Mutex;
        u32 OutboundSequence = 1;
        u32 InboundSequence = 1;
        std::vector<u8> OutboundBatch;
        std::vector<u8> InboundQueue;
        std::vector<BinaryTransformPayload> PendingTransformMutations;
        std::optional<Scene::EntityId> LastSelectedEntity;
        std::vector<std::string> PendingCommands;
        bool HasPendingViewportPick = false;
        f32 PendingPickX = 0.0f;
        f32 PendingPickY = 0.0f;
        BinaryIPCStats Stats{};
    };

    BinaryIPCBridge::BinaryIPCBridge()
        : m_impl(std::make_unique<Impl>())
    {
    }

    BinaryIPCBridge::~BinaryIPCBridge()
    {
        Shutdown();
    }

    bool BinaryIPCBridge::Initialize(std::string_view sharedMemoryName, usize ringCapacityBytes)
    {
        Shutdown();

        std::lock_guard lock(m_impl->Mutex);
        m_impl->SharedMemoryName = std::string(sharedMemoryName);
        m_impl->CapacityBytes = ringCapacityBytes;
        m_impl->OutboundBatch.reserve(64 * 1024);
        m_impl->InboundQueue.reserve(64 * 1024);

#if defined(_WIN32)
        const DWORD totalMappingBytes = static_cast<DWORD>(sizeof(SharedMemoryControlBlock) + ringCapacityBytes);
        HANDLE hMap = CreateFileMappingA(
            INVALID_HANDLE_VALUE,
            nullptr,
            PAGE_READWRITE,
            0,
            totalMappingBytes,
            m_impl->SharedMemoryName.c_str());

        if (hMap != nullptr)
        {
            void* view = MapViewOfFile(hMap, FILE_MAP_ALL_ACCESS, 0, 0, totalMappingBytes);
            if (view != nullptr)
            {
                m_impl->FileMappingHandle = hMap;
                m_impl->MappedBasePtr = static_cast<u8*>(view);
                m_impl->Stats.SharedMemoryMapped = true;

                auto* ctrl = reinterpret_cast<SharedMemoryControlBlock*>(m_impl->MappedBasePtr);
                ctrl->Magic = kBinaryIPCMagic;
                ctrl->CapacityBytes = static_cast<u32>(ringCapacityBytes);
                ctrl->WriteCursor = 0;
                ctrl->SequenceCounter = 0;
            }
            else
            {
                CloseHandle(hMap);
            }
        }
#endif

        Core::Log::Info("UI.BinaryIPC", "Initialized Zero-Parse Binary IPC & Shared Memory Ring Buffer ('" +
            m_impl->SharedMemoryName + "', Capacity=" + std::to_string(ringCapacityBytes / 1024) +
            " KB, Mapped=" + (m_impl->Stats.SharedMemoryMapped ? "YES" : "NO") + ")");
        return true;
    }

    void BinaryIPCBridge::Shutdown()
    {
        if (!m_impl)
        {
            return;
        }

        std::lock_guard lock(m_impl->Mutex);
#if defined(_WIN32)
        if (m_impl->MappedBasePtr != nullptr)
        {
            UnmapViewOfFile(m_impl->MappedBasePtr);
            m_impl->MappedBasePtr = nullptr;
        }
        if (m_impl->FileMappingHandle != nullptr)
        {
            CloseHandle(static_cast<HANDLE>(m_impl->FileMappingHandle));
            m_impl->FileMappingHandle = nullptr;
        }
#endif
        m_impl->OutboundBatch.clear();
        m_impl->InboundQueue.clear();
        m_impl->PendingTransformMutations.clear();
        m_impl->Stats.SharedMemoryMapped = false;
    }

    bool BinaryIPCBridge::PushPacket(BinaryIPCOpcode opcode, std::span<const u8> payload)
    {
        MORBI_BREADCRUMB("BinaryIPC RingBuffer Read/Write");
        std::lock_guard lock(m_impl->Mutex);

        BinaryPacketHeader header{};
        header.Magic = kBinaryIPCMagic;
        header.Opcode = static_cast<u16>(opcode);
        header.Version = kBinaryIPCVersion;
        header.SequenceId = m_impl->OutboundSequence++;
        header.PayloadSizeBytes = static_cast<u32>(payload.size());

        const usize totalPacketBytes = sizeof(BinaryPacketHeader) + payload.size();
        const usize oldSize = m_impl->OutboundBatch.size();
        m_impl->OutboundBatch.resize(oldSize + totalPacketBytes);

        std::memcpy(m_impl->OutboundBatch.data() + oldSize, &header, sizeof(BinaryPacketHeader));
        if (!payload.empty())
        {
            std::memcpy(
                m_impl->OutboundBatch.data() + oldSize + sizeof(BinaryPacketHeader),
                payload.data(),
                payload.size());
        }

        // Mirror packet into the Named Shared Memory Ring Buffer for external WebView2 / Hub consumers
        if (m_impl->MappedBasePtr != nullptr && totalPacketBytes <= m_impl->CapacityBytes)
        {
            auto* ctrl = reinterpret_cast<SharedMemoryControlBlock*>(m_impl->MappedBasePtr);
            u8* ringData = m_impl->MappedBasePtr + sizeof(SharedMemoryControlBlock);
            if (ctrl->WriteCursor + totalPacketBytes > m_impl->CapacityBytes)
            {
                ctrl->WriteCursor = 0;
            }
            std::memcpy(ringData + ctrl->WriteCursor, m_impl->OutboundBatch.data() + oldSize, totalPacketBytes);
            ctrl->WriteCursor += static_cast<u32>(totalPacketBytes);
            ctrl->SequenceCounter = header.SequenceId;
            MORBI_TRACE("UI.BinaryIPC", "RingWrite Opcode=" + std::to_string(header.Opcode) +
                " Seq=" + std::to_string(header.SequenceId) +
                " Bytes=" + std::to_string(totalPacketBytes) +
                " WriteCursor=" + std::to_string(ctrl->WriteCursor));
        }

        ++m_impl->Stats.PacketsSentToJS;
        m_impl->Stats.BytesSentToJS += totalPacketBytes;
        return true;
    }

    bool BinaryIPCBridge::PushTransformUpdate(Scene::EntityId entity, const Scene::TransformComponent& transform)
    {
        BinaryTransformPayload payload{};
        payload.PackedEntityId = entity.Pack();
        payload.Position[0] = transform.Position.x;
        payload.Position[1] = transform.Position.y;
        payload.Position[2] = transform.Position.z;
        payload.RotationQuat[0] = transform.Rotation.x;
        payload.RotationQuat[1] = transform.Rotation.y;
        payload.RotationQuat[2] = transform.Rotation.z;
        payload.RotationQuat[3] = transform.Rotation.w;
        payload.Scale[0] = transform.Scale.x;
        payload.Scale[1] = transform.Scale.y;
        payload.Scale[2] = transform.Scale.z;

        return PushPacket(
            BinaryIPCOpcode::SyncEntityTransform,
            std::span<const u8>(reinterpret_cast<const u8*>(&payload), sizeof(payload)));
    }

    bool BinaryIPCBridge::PushTelemetry(const BinaryTelemetryPayload& telemetry)
    {
        return PushPacket(
            BinaryIPCOpcode::EngineTelemetrySnapshot,
            std::span<const u8>(reinterpret_cast<const u8*>(&telemetry), sizeof(telemetry)));
    }

    bool BinaryIPCBridge::PushViewportSharedTexture(u32 width, u32 height, f32 dpiScale, u64 sharedTextureHandle)
    {
        BinaryViewportResizePayload payload{
            .Width = width,
            .Height = height,
            .DpiScale = dpiScale,
            .Reserved = 0,
            .SharedTextureHandle = sharedTextureHandle
        };
        return PushPacket(
            BinaryIPCOpcode::ViewportResize,
            std::span<const u8>(reinterpret_cast<const u8*>(&payload), sizeof(payload)));
    }

    bool BinaryIPCBridge::PushEntitySelected(Scene::EntityId entity)
    {
        const u64 packed = entity.Pack();
        return PushPacket(
            BinaryIPCOpcode::SelectEntity,
            std::span<const u8>(reinterpret_cast<const u8*>(&packed), sizeof(packed)));
    }

    bool BinaryIPCBridge::EnqueueIncomingBinaryFrame(std::span<const u8> rawFrameBytes)
    {
        if (rawFrameBytes.size() < sizeof(BinaryPacketHeader))
        {
            return false;
        }

        std::lock_guard lock(m_impl->Mutex);
        m_impl->InboundQueue.insert(m_impl->InboundQueue.end(), rawFrameBytes.begin(), rawFrameBytes.end());
        return true;
    }

    bool BinaryIPCBridge::SimulateJSTransformDrag(Scene::EntityId entity, const Math::Vec3& newPosition)
    {
        BinaryPacketHeader header{};
        header.Magic = kBinaryIPCMagic;
        header.Opcode = static_cast<u16>(BinaryIPCOpcode::SyncEntityTransform);
        header.Version = kBinaryIPCVersion;
        header.SequenceId = m_impl->InboundSequence++;
        header.PayloadSizeBytes = sizeof(BinaryTransformPayload);

        BinaryTransformPayload payload{};
        payload.PackedEntityId = entity.Pack();
        payload.Position[0] = newPosition.x;
        payload.Position[1] = newPosition.y;
        payload.Position[2] = newPosition.z;
        payload.RotationQuat[0] = 0.0f;
        payload.RotationQuat[1] = 0.0f;
        payload.RotationQuat[2] = 0.0f;
        payload.RotationQuat[3] = 1.0f;
        payload.Scale[0] = 1.0f;
        payload.Scale[1] = 1.0f;
        payload.Scale[2] = 1.0f;

        std::vector<u8> frame(sizeof(header) + sizeof(payload));
        std::memcpy(frame.data(), &header, sizeof(header));
        std::memcpy(frame.data() + sizeof(header), &payload, sizeof(payload));

        return EnqueueIncomingBinaryFrame(frame);
    }

    bool BinaryIPCBridge::SimulateJSViewportPick(f32 normX, f32 normY)
    {
        BinaryPacketHeader header{};
        header.Magic = kBinaryIPCMagic;
        header.Opcode = static_cast<u16>(BinaryIPCOpcode::ViewportPick);
        header.Version = kBinaryIPCVersion;
        header.SequenceId = m_impl->InboundSequence++;
        header.PayloadSizeBytes = sizeof(BinaryViewportPickPayload);

        BinaryViewportPickPayload payload{ .NormalizedX = normX, .NormalizedY = normY };

        std::vector<u8> frame(sizeof(header) + sizeof(payload));
        std::memcpy(frame.data(), &header, sizeof(header));
        std::memcpy(frame.data() + sizeof(header), &payload, sizeof(payload));

        return EnqueueIncomingBinaryFrame(frame);
    }

    usize BinaryIPCBridge::PollIncomingPackets(const PacketCallback& callback)
    {
        std::vector<u8> localBuffer;
        {
            std::lock_guard lock(m_impl->Mutex);
            if (m_impl->InboundQueue.empty())
            {
                return 0;
            }
            localBuffer.swap(m_impl->InboundQueue);
        }

        MORBI_BREADCRUMB("BinaryIPC RingBuffer Read/Write");
        MORBI_TRACE("UI.BinaryIPC", "RingRead Polling " + std::to_string(localBuffer.size()) + " inbound bytes");

        usize offset = 0;
        usize processedPackets = 0;

        while (offset + sizeof(BinaryPacketHeader) <= localBuffer.size())
        {
            BinaryPacketHeader header{};
            std::memcpy(&header, localBuffer.data() + offset, sizeof(BinaryPacketHeader));
            offset += sizeof(BinaryPacketHeader);

            if (header.Magic != kBinaryIPCMagic || offset + header.PayloadSizeBytes > localBuffer.size())
            {
                break;
            }

            const std::span<const u8> payload(localBuffer.data() + offset, header.PayloadSizeBytes);
            offset += header.PayloadSizeBytes;

            const auto opcode = static_cast<BinaryIPCOpcode>(header.Opcode);
            if (opcode == BinaryIPCOpcode::SyncEntityTransform &&
                payload.size() == sizeof(BinaryTransformPayload))
            {
                BinaryTransformPayload mut{};
                std::memcpy(&mut, payload.data(), sizeof(BinaryTransformPayload));
                std::lock_guard lock(m_impl->Mutex);
                m_impl->PendingTransformMutations.push_back(mut);
            }
            else if (opcode == BinaryIPCOpcode::SelectEntity &&
                     payload.size() == sizeof(u64))
            {
                u64 packed = 0;
                std::memcpy(&packed, payload.data(), sizeof(u64));
                std::lock_guard lock(m_impl->Mutex);
                m_impl->LastSelectedEntity = Scene::EntityId::Unpack(packed);
                ++m_impl->Stats.EntitySelectionsProcessed;
            }
            else if (opcode == BinaryIPCOpcode::ViewportPick &&
                     payload.size() == sizeof(BinaryViewportPickPayload))
            {
                BinaryViewportPickPayload pick{};
                std::memcpy(&pick, payload.data(), sizeof(BinaryViewportPickPayload));
                std::lock_guard lock(m_impl->Mutex);
                ++m_impl->Stats.ViewportPicksProcessed;
                m_impl->HasPendingViewportPick = true;
                m_impl->PendingPickX = pick.NormalizedX;
                m_impl->PendingPickY = pick.NormalizedY;
            }
            else if (opcode == BinaryIPCOpcode::ExecuteCommand && !payload.empty())
            {
                std::string cmd(reinterpret_cast<const char*>(payload.data()), payload.size());
                std::lock_guard lock(m_impl->Mutex);
                m_impl->PendingCommands.push_back(std::move(cmd));
            }

            if (callback)
            {
                callback(header, payload);
            }

            {
                std::lock_guard lock(m_impl->Mutex);
                ++m_impl->Stats.PacketsReceivedFromJS;
                m_impl->Stats.BytesReceivedFromJS += sizeof(BinaryPacketHeader) + header.PayloadSizeBytes;
            }
            ++processedPackets;
        }

        return processedPackets;
    }

    u32 BinaryIPCBridge::ApplyIncomingTransformMutations(Scene::Scene& scene)
    {
        std::vector<BinaryTransformPayload> mutations;
        {
            std::lock_guard lock(m_impl->Mutex);
            if (m_impl->PendingTransformMutations.empty())
            {
                return 0;
            }
            mutations.swap(m_impl->PendingTransformMutations);
        }

        u32 applied = 0;
        for (const auto& mut : mutations)
        {
            const Scene::EntityId entity = Scene::EntityId::Unpack(mut.PackedEntityId);
            if (auto* tr = scene.Transforms().TryGet(entity))
            {
                tr->Position = { mut.Position[0], mut.Position[1], mut.Position[2] };
                tr->Rotation = { mut.RotationQuat[0], mut.RotationQuat[1], mut.RotationQuat[2], mut.RotationQuat[3] };
                tr->Scale = { mut.Scale[0], mut.Scale[1], mut.Scale[2] };
                if (auto* rb = scene.RigidBodies().TryGet(entity))
                {
                    rb->DirtyFlags |= static_cast<u8>(Scene::RigidBodyDirtyFlags::Transform);
                }
                ++applied;
            }
        }

        if (applied > 0)
        {
            std::lock_guard lock(m_impl->Mutex);
            m_impl->Stats.ZeroCopyTransformMutations += applied;
        }
        return applied;
    }

    std::span<const u8> BinaryIPCBridge::GetOutboundBatchBytes() const noexcept
    {
        return m_impl->OutboundBatch;
    }

    void BinaryIPCBridge::ClearOutboundBatch() noexcept
    {
        std::lock_guard lock(m_impl->Mutex);
        m_impl->OutboundBatch.clear();
    }

    BinaryIPCStats BinaryIPCBridge::GetStats() const noexcept
    {
        std::lock_guard lock(m_impl->Mutex);
        return m_impl->Stats;
    }

    void BinaryIPCBridge::SetSelectedEntity(std::optional<Scene::EntityId> entity) noexcept
    {
        std::lock_guard lock(m_impl->Mutex);
        m_impl->LastSelectedEntity = entity;
    }

    std::optional<Scene::EntityId> BinaryIPCBridge::GetLastSelectedEntity() const noexcept
    {
        std::lock_guard lock(m_impl->Mutex);
        return m_impl->LastSelectedEntity;
    }

    bool BinaryIPCBridge::ConsumePendingViewportPick(f32& outNormX, f32& outNormY) noexcept
    {
        std::lock_guard lock(m_impl->Mutex);
        if (!m_impl->HasPendingViewportPick) return false;
        outNormX = m_impl->PendingPickX;
        outNormY = m_impl->PendingPickY;
        m_impl->HasPendingViewportPick = false;
        return true;
    }

    bool BinaryIPCBridge::ConsumePendingCommand(std::string& outCommand) noexcept
    {
        std::lock_guard lock(m_impl->Mutex);
        if (m_impl->PendingCommands.empty()) return false;
        outCommand = std::move(m_impl->PendingCommands.front());
        m_impl->PendingCommands.erase(m_impl->PendingCommands.begin());
        return true;
    }

    void* BinaryIPCBridge::GetSharedMemoryPointer() const noexcept
    {
        return m_impl->MappedBasePtr;
    }
}
