#include "Morbi/Runtime/SceneReplicationBridge.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <unordered_set>

#include "Morbi/Core/Log.hpp"
#include "Morbi/Physics/PhysicsWorld.hpp"

namespace Morbi::Runtime
{
    namespace
    {
        [[nodiscard]] u64 NowSteadyMs() noexcept
        {
            return static_cast<u64>(std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count());
        }

        [[nodiscard]] u64 NowSteadyUs() noexcept
        {
            return static_cast<u64>(std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count());
        }

        template <typename T>
        [[nodiscard]] bool ReadLE(const u8*& ptr, const u8* end, T& outVal) noexcept
        {
            if (static_cast<usize>(end - ptr) < sizeof(T))
            {
                return false;
            }
            std::memcpy(&outVal, ptr, sizeof(T));
            ptr += sizeof(T);
            return true;
        }

        template <typename T>
        void WriteLE(std::vector<u8>& out, const T& val)
        {
            const usize pos = out.size();
            out.resize(pos + sizeof(T));
            std::memcpy(out.data() + pos, &val, sizeof(T));
        }

        [[nodiscard]] bool ReadManagedValueFromBytes(
            const u8*& ptr,
            const u8* end,
            Network::ManagedValue& outVal) noexcept
        {
            u8 typeByte = 0;
            if (!ReadLE(ptr, end, typeByte))
            {
                return false;
            }
            outVal = Network::ManagedValue{};
            outVal.Type = static_cast<Network::ManagedValueType>(typeByte);
            switch (outVal.Type)
            {
            case Network::ManagedValueType::Int32:
                if (!ReadLE(ptr, end, outVal.IntVal)) return false;
                break;
            case Network::ManagedValueType::Float32:
                if (!ReadLE(ptr, end, outVal.FloatVal)) return false;
                break;
            case Network::ManagedValueType::Boolean:
            {
                u8 b = 0;
                if (!ReadLE(ptr, end, b)) return false;
                outVal.BoolVal = (b != 0);
                break;
            }
            case Network::ManagedValueType::Vec3:
                if (!ReadLE(ptr, end, outVal.VecX) ||
                    !ReadLE(ptr, end, outVal.VecY) ||
                    !ReadLE(ptr, end, outVal.VecZ))
                {
                    return false;
                }
                break;
            case Network::ManagedValueType::String:
            {
                u16 len = 0;
                if (!ReadLE(ptr, end, len) ||
                    len > Network::ReplicationConstants::kMaxManagedStringBytes ||
                    static_cast<usize>(end - ptr) < len)
                {
                    return false;
                }
                outVal.StringVal.assign(reinterpret_cast<const char*>(ptr), len);
                ptr += len;
                break;
            }
            case Network::ManagedValueType::UInt64:
                if (!ReadLE(ptr, end, outVal.UInt64Val)) return false;
                break;
            default:
                return false;
            }
            return outVal.IsValid();
        }

        [[nodiscard]] bool WriteManagedValueToBytes(
            const Network::ManagedValue& val,
            std::vector<u8>& outBytes)
        {
            if (!val.IsValid())
            {
                return false;
            }
            outBytes.push_back(static_cast<u8>(val.Type));
            switch (val.Type)
            {
            case Network::ManagedValueType::Int32:
                WriteLE(outBytes, val.IntVal);
                return true;
            case Network::ManagedValueType::Float32:
                WriteLE(outBytes, val.FloatVal);
                return true;
            case Network::ManagedValueType::Boolean:
                outBytes.push_back(val.BoolVal ? u8{1} : u8{0});
                return true;
            case Network::ManagedValueType::Vec3:
                WriteLE(outBytes, val.VecX);
                WriteLE(outBytes, val.VecY);
                WriteLE(outBytes, val.VecZ);
                return true;
            case Network::ManagedValueType::String:
            {
                if (val.StringVal.size() > Network::ReplicationConstants::kMaxManagedStringBytes)
                {
                    return false;
                }
                const u16 len = static_cast<u16>(val.StringVal.size());
                WriteLE(outBytes, len);
                outBytes.insert(outBytes.end(), val.StringVal.begin(), val.StringVal.end());
                return true;
            }
            case Network::ManagedValueType::UInt64:
                WriteLE(outBytes, val.UInt64Val);
                return true;
            default:
                return false;
            }
        }

        [[nodiscard]] bool DecodeManagedPropsBytes(
            std::span<const u8> bytes,
            std::vector<Network::ManagedPropertyEntry>& outProps)
        {
            outProps.clear();
            const u8* ptr = bytes.data();
            const u8* end = ptr + bytes.size();
            u16 count = 0;
            if (!ReadLE(ptr, end, count) ||
                count > Network::ReplicationConstants::kMaxManagedPropertiesPerSync)
            {
                return false;
            }
            outProps.reserve(count);
            for (u16 i = 0; i < count; ++i)
            {
                Network::ManagedPropertyEntry entry{};
                if (!ReadLE(ptr, end, entry.PropertyId) ||
                    !ReadManagedValueFromBytes(ptr, end, entry.Value))
                {
                    outProps.clear();
                    return false;
                }
                outProps.push_back(std::move(entry));
            }
            return ptr == end;
        }

        [[nodiscard]] bool EncodeManagedPropsBytes(
            const std::vector<Network::ManagedPropertyEntry>& props,
            std::vector<u8>& outBytes)
        {
            if (props.size() > Network::ReplicationConstants::kMaxManagedPropertiesPerSync)
            {
                return false;
            }
            outBytes.clear();
            const u16 count = static_cast<u16>(props.size());
            WriteLE(outBytes, count);
            for (const auto& entry : props)
            {
                WriteLE(outBytes, entry.PropertyId);
                if (!WriteManagedValueToBytes(entry.Value, outBytes))
                {
                    outBytes.clear();
                    return false;
                }
            }
            return true;
        }

        [[nodiscard]] bool DecodeManagedRpcArgsBytes(
            std::span<const u8> bytes,
            std::vector<Network::ManagedValue>& outArgs)
        {
            outArgs.clear();
            if (bytes.empty())
            {
                return true;
            }
            const u8* ptr = bytes.data();
            const u8* end = ptr + bytes.size();
            u8 count = 0;
            if (!ReadLE(ptr, end, count) ||
                count > Network::ReplicationConstants::kMaxManagedRpcArgs)
            {
                return false;
            }
            outArgs.reserve(count);
            for (u8 i = 0; i < count; ++i)
            {
                Network::ManagedValue val{};
                if (!ReadManagedValueFromBytes(ptr, end, val))
                {
                    outArgs.clear();
                    return false;
                }
                outArgs.push_back(std::move(val));
            }
            return ptr == end;
        }

        [[nodiscard]] bool EncodeManagedRpcArgsBytes(
            const std::vector<Network::ManagedValue>& args,
            std::vector<u8>& outBytes)
        {
            if (args.size() > Network::ReplicationConstants::kMaxManagedRpcArgs)
            {
                return false;
            }
            outBytes.clear();
            outBytes.push_back(static_cast<u8>(args.size()));
            for (const auto& val : args)
            {
                if (!WriteManagedValueToBytes(val, outBytes))
                {
                    outBytes.clear();
                    return false;
                }
            }
            return true;
        }

        /// Shared deterministic character movement rule used by both authoritative server
        /// input processing and client-side prediction/reconciliation replay.
        void ApplyCharacterInputCommand(
            const Network::PlayerInputCommand& cmd,
            f32 fixedDt,
            f32 moveSpeed,
            f32 jumpSpeed,
            Scene::Scene& scene,
            Scene::EntityId eid,
            Physics::PhysicsWorld* physics,
            Network::ReplicatedMovementData& inOutMov)
        {
            auto* tf = scene.Transforms().TryGet(eid);
            if (tf == nullptr)
            {
                return;
            }

            f32 mx = cmd.MoveX;
            f32 mz = cmd.MoveZ;
            const f32 lenSq = mx * mx + mz * mz;
            if (lenSq > 1.0f)
            {
                const f32 inv = 1.0f / std::sqrt(lenSq);
                mx *= inv;
                mz *= inv;
            }
            const f32 speedScale = ((cmd.ActionFlags & Network::InputActionFlags::kSprint) != 0) ? 1.5f : 1.0f;
            const f32 vx = mx * moveSpeed * speedScale;
            const f32 vz = mz * moveSpeed * speedScale;

            if (auto* cc = scene.CharacterControllers().TryGet(eid); cc != nullptr && physics != nullptr)
            {
                cc->DesiredHorizontalVel = Math::Vec3{ vx, 0.0f, vz };
                tf->Rotation = Math::FromAxisAngle(Math::Vec3::Up(), cmd.YawRadians);
                if ((cmd.ActionFlags & Network::InputActionFlags::kJump) != 0 && cc->IsGrounded != 0u)
                {
                    (void)physics->JumpCharacter(scene, eid, jumpSpeed);
                }
                inOutMov.VelX = vx;
                inOutMov.VelY = cc->Velocity.y;
                inOutMov.VelZ = vz;
                inOutMov.YawRadians = cmd.YawRadians;
                inOutMov.LastProcessedInputSeq = cmd.Sequence;
                inOutMov.MovementFlags = static_cast<u8>((lenSq > 1e-4f ? 1u : 0u) |
                    ((cmd.ActionFlags & Network::InputActionFlags::kJump) != 0 ? 2u : 0u) |
                    (cc->IsGrounded != 0u ? 4u : 0u));
                return;
            }

            tf->Position.x = std::clamp(tf->Position.x + vx * fixedDt, -90000.0f, 90000.0f);
            tf->Position.z = std::clamp(tf->Position.z + vz * fixedDt, -90000.0f, 90000.0f);
            tf->Rotation = Math::FromAxisAngle(Math::Vec3::Up(), cmd.YawRadians);

            inOutMov.VelX = vx;
            inOutMov.VelY = 0.0f;
            inOutMov.VelZ = vz;
            inOutMov.YawRadians = cmd.YawRadians;
            inOutMov.LastProcessedInputSeq = cmd.Sequence;
            inOutMov.MovementFlags = static_cast<u8>((lenSq > 1e-4f ? 1u : 0u) |
                ((cmd.ActionFlags & Network::InputActionFlags::kJump) != 0 ? 2u : 0u));
        }
    }

    bool SceneReplicationBridge::SceneToReplicatedTransform(
        const Scene::TransformComponent& src,
        Network::ReplicatedTransformData& outDst) noexcept
    {
        outDst.PosX = src.Position.x;
        outDst.PosY = src.Position.y;
        outDst.PosZ = src.Position.z;
        outDst.RotX = src.Rotation.x;
        outDst.RotY = src.Rotation.y;
        outDst.RotZ = src.Rotation.z;
        outDst.RotW = src.Rotation.w;
        outDst.ScaleX = (std::max)(src.Scale.x, Network::ReplicationConstants::kMinScale);
        outDst.ScaleY = (std::max)(src.Scale.y, Network::ReplicationConstants::kMinScale);
        outDst.ScaleZ = (std::max)(src.Scale.z, Network::ReplicationConstants::kMinScale);
        return outDst.IsValid();
    }

    bool SceneReplicationBridge::ReplicatedToSceneTransform(
        const Network::ReplicatedTransformData& src,
        Scene::TransformComponent& outDst) noexcept
    {
        if (!src.IsValid())
        {
            return false;
        }
        outDst.Position = Math::Vec3{ src.PosX, src.PosY, src.PosZ };
        outDst.Rotation = Math::Quat{ src.RotX, src.RotY, src.RotZ, src.RotW };
        outDst.Scale = Math::Vec3{ src.ScaleX, src.ScaleY, src.ScaleZ };
        return true;
    }

    void SceneReplicationBridge::Initialize(
        bool isServer,
        const ReplicationLaunchConfig& config,
        Network::ConnectionManager& net)
    {
        (void)net;
        m_isServer = isServer;
        m_config = config;
        m_spawnCounter = 0;
        m_playerActionRpcCount = 0;
        m_lastInputProcessedTick = static_cast<u64>(-1);
        m_netToScene.clear();
        m_sceneToNet.clear();
        m_staticToNet.clear();
        m_netToStatic.clear();
        m_serverManagedPropCache.clear();
        m_pendingPropReplayConns.clear();
        m_serverRep.Reset();
        m_clientApplier.Reset();
        m_inputPipeline.Reset();
        m_rpcRouter.Clear();
        m_rpcRouter.SetServerRole(isServer);

        m_predictionStats = ClientPredictionStats{};
        m_localPlayerId = Network::kInvalidPlayerId;
        m_localOwnedNetId = Network::kInvalidNetworkId;
        m_clientInputHistory.Reset();
        m_clientInputHistory.SetCapacity(m_predictionConfig.InputHistoryCapacity);
        m_remoteInterp.Clear();
        m_remoteInterp.SetInterpolationDelaySec(m_predictionConfig.InterpolationDelaySec);
        m_clientPhysics = nullptr;
        m_logicalPredictedPos = Math::Vec3::Zero();
        m_visualCorrectionOffset = Math::Vec3::Zero();
        m_nextClientInputSeq = 1;
        m_clientPredictionTick = 1;
        m_clientClockSec = 0.0;
        m_lastClientSnapshotTick = 0;
        m_activeClientWorldId = Network::kInvalidWorldInstanceId;
        m_clientInterpolationDriven = false;

        if (config.Interest == Network::InterestMode::Distance)
        {
            m_serverRep.SetInterestPolicy(std::make_shared<Network::DistanceInterestPolicy>(
                config.InterestEnterRadius,
                config.InterestLeaveRadius));
        }
        else
        {
            m_serverRep.SetInterestPolicy(std::make_shared<Network::AllVisibleInterestPolicy>());
        }

        m_initialized = true;
    }

    void SceneReplicationBridge::Shutdown(Scene::Scene& scene, Network::ConnectionManager& net)
    {
        if (!m_initialized)
        {
            return;
        }

        if (m_isServer)
        {
            auto& sessions = net.GetSessionRegistry();
            std::vector<Network::NetworkId> ids;
            ids.reserve(m_netToScene.size());
            for (const auto& [packed, eid] : m_netToScene)
            {
                (void)eid;
                ids.push_back(Network::NetworkId::Unpack(packed));
            }
            for (const auto netId : ids)
            {
                (void)DespawnReplicatedEntity(scene, sessions, netId);
            }
        }
        else
        {
            std::vector<Scene::EntityId> sceneIds;
            sceneIds.reserve(m_netToScene.size());
            for (const auto& [packed, eid] : m_netToScene)
            {
                (void)packed;
                sceneIds.push_back(eid);
            }
            for (const auto eid : sceneIds)
            {
                if (scene.IsAlive(eid))
                {
                    scene.DestroyEntity(eid);
                }
            }
        }

        m_netToScene.clear();
        m_sceneToNet.clear();
        m_staticToNet.clear();
        m_netToStatic.clear();
        m_serverManagedPropCache.clear();
        m_pendingPropReplayConns.clear();
        m_serverRep.Reset();
        m_clientApplier.Reset();
        m_inputPipeline.Reset();
        m_rpcRouter.Clear();
        m_clientInputHistory.Reset();
        m_remoteInterp.Clear();
        m_localOwnedNetId = Network::kInvalidNetworkId;
        m_clientPhysics = nullptr;
        m_visualCorrectionOffset = Math::Vec3::Zero();
        m_initialized = false;
    }

    void SceneReplicationBridge::RegisterBuiltinRpcs(Scene::Scene& scene, Network::ConnectionManager& net)
    {
        (void)m_rpcRouter.RegisterRpc(
            Network::RpcDescriptor{
                .RpcId = BuiltinRpcIds::kRequestDespawnSelf,
                .Name = "RequestDespawnSelf",
                .Direction = Network::RpcDirection::ClientToServer,
                .Authority = Network::RpcAuthorityRule::OwnerOnly,
                .Delivery = Network::NetDelivery::ReliableOrdered,
                .Channel = Network::NetChannel::GameplayEvents,
                .MaxPayloadBytes = 64,
                .RateLimitPerSec = 5
            },
            [this, &scene, &net](const Network::RpcInvocationContext& ctx) {
                if (ctx.TargetEntity.IsValid())
                {
                    (void)DespawnReplicatedEntity(scene, net.GetSessionRegistry(), ctx.TargetEntity);
                }
            });

        (void)m_rpcRouter.RegisterRpc(
            Network::RpcDescriptor{
                .RpcId = BuiltinRpcIds::kPlayerEmoteAction,
                .Name = "PlayerEmoteAction",
                .Direction = Network::RpcDirection::ClientToServer,
                .Authority = Network::RpcAuthorityRule::OwnerOnly,
                .Delivery = Network::NetDelivery::ReliableOrdered,
                .Channel = Network::NetChannel::GameplayEvents,
                .MaxPayloadBytes = 64,
                .RateLimitPerSec = 10
            },
            [this](const Network::RpcInvocationContext&) {
                ++m_playerActionRpcCount;
            });

        (void)m_rpcRouter.RegisterRpc(
            Network::RpcDescriptor{
                .RpcId = BuiltinRpcIds::kServerOnlyAdminReset,
                .Name = "ServerOnlyAdminReset",
                .Direction = Network::RpcDirection::ClientToServer,
                .Authority = Network::RpcAuthorityRule::ServerOnly,
                .Delivery = Network::NetDelivery::ReliableOrdered,
                .Channel = Network::NetChannel::GameplayEvents,
                .MaxPayloadBytes = 64,
                .RateLimitPerSec = 2
            },
            [](const Network::RpcInvocationContext&) {
                // Never reachable from clients due to RpcAuthorityRule::ServerOnly
            });

        (void)m_rpcRouter.RegisterRpc(
            Network::RpcDescriptor{
                .RpcId = BuiltinRpcIds::kManagedServerRpc,
                .Name = "ManagedServerRpc",
                .Direction = Network::RpcDirection::ClientToServer,
                .Authority = Network::RpcAuthorityRule::AnyAuthenticatedPeer,
                .Delivery = Network::NetDelivery::ReliableOrdered,
                .Channel = Network::NetChannel::GameplayEvents,
                .MaxPayloadBytes = 2048,
                .RateLimitPerSec = 60
            },
            [this](const Network::RpcInvocationContext& ctx) {
                if (!m_isServer || !m_onManagedRpc)
                {
                    return;
                }
                Network::BitReader reader(ctx.Payload);
                Network::ManagedRpcPacket pkt{};
                if (!Network::ReplicationCodec::ReadManagedRpc(reader, pkt))
                {
                    return;
                }
                if (pkt.WorldId != m_config.WorldId || pkt.TargetNetId != ctx.TargetEntity)
                {
                    return;
                }
                Network::ServerReplicatedEntity ent{};
                if (!m_serverRep.TryGetEntity(pkt.TargetNetId, ent))
                {
                    return;
                }
                if (pkt.RequireOwnership)
                {
                    if (!ctx.SenderSession.IsValid() || ent.OwnerSession != ctx.SenderSession ||
                        ctx.SenderPlayer == Network::kInvalidPlayerId || ent.OwnerPlayer != ctx.SenderPlayer)
                    {
                        return;
                    }
                }
                std::vector<u8> argsBytes;
                if (!EncodeManagedRpcArgsBytes(pkt.Args, argsBytes))
                {
                    return;
                }
                (void)m_onManagedRpc(
                    pkt.WorldId,
                    pkt.TargetNetId,
                    pkt.BehaviourTypeHash,
                    pkt.MethodHash,
                    ctx.SenderPlayer,
                    true,
                    argsBytes);
            });

        (void)m_rpcRouter.RegisterRpc(
            Network::RpcDescriptor{
                .RpcId = BuiltinRpcIds::kManagedClientRpc,
                .Name = "ManagedClientRpc",
                .Direction = Network::RpcDirection::ServerToClient,
                .Authority = Network::RpcAuthorityRule::ServerOnly,
                .Delivery = Network::NetDelivery::ReliableOrdered,
                .Channel = Network::NetChannel::GameplayEvents,
                .MaxPayloadBytes = 2048,
                .RateLimitPerSec = 120
            },
            [this](const Network::RpcInvocationContext& ctx) {
                if (m_isServer || !m_onManagedRpc)
                {
                    return;
                }
                Network::BitReader reader(ctx.Payload);
                Network::ManagedRpcPacket pkt{};
                if (!Network::ReplicationCodec::ReadManagedRpc(reader, pkt))
                {
                    return;
                }
                const Network::WorldInstanceId activeW =
                    (m_activeClientWorldId != Network::kInvalidWorldInstanceId) ? m_activeClientWorldId : m_config.WorldId;
                if (pkt.WorldId != activeW || pkt.TargetNetId != ctx.TargetEntity)
                {
                    return;
                }
                Network::ClientReplicatedEntity clientEnt{};
                if (!m_clientApplier.TryGetEntity(pkt.TargetNetId, clientEnt))
                {
                    return;
                }
                std::vector<u8> argsBytes;
                if (!EncodeManagedRpcArgsBytes(pkt.Args, argsBytes))
                {
                    return;
                }
                (void)m_onManagedRpc(
                    pkt.WorldId,
                    pkt.TargetNetId,
                    pkt.BehaviourTypeHash,
                    pkt.MethodHash,
                    Network::kInvalidPlayerId,
                    false,
                    argsBytes);
            });
    }

    Network::NetworkId SceneReplicationBridge::SpawnReplicatedEntity(
        Scene::Scene& scene,
        Network::SessionRegistry& sessions,
        const Network::ReplicatedEntitySpawnDesc& desc,
        u64 serverTick)
    {
        if (!m_initialized || !m_isServer)
        {
            return Network::kInvalidNetworkId;
        }
        if (m_rpcRouter.IsRegistered(BuiltinRpcIds::kRequestDespawnSelf) == false)
        {
            // Ensure built-ins exist when caller doesn't invoke OnServerClientAuthenticated first
            // Registered cleanly below in SimulationSession via RegisterBuiltinRpcs if needed
        }

        const Network::NetworkId netId = m_serverRep.SpawnEntity(desc, serverTick);
        if (!netId.IsValid())
        {
            return Network::kInvalidNetworkId;
        }

        const Scene::EntityId eid = scene.CreateEntity(desc.EntityName);
        if (auto* tf = scene.Transforms().TryGet(eid))
        {
            (void)ReplicatedToSceneTransform(desc.InitialTransform, *tf);
        }

        m_netToScene[netId.Pack()] = eid;
        m_sceneToNet[eid.Pack()] = netId;

        if (desc.OwnerSession.IsValid())
        {
            (void)sessions.BindEntity(Network::NetworkEntityBinding{
                .NetId = netId,
                .OwnerSession = desc.OwnerSession,
                .OwnerPlayer = desc.OwnerPlayer,
                .WorldId = desc.WorldId,
                .SceneEntityIndex = eid.Index,
                .SceneEntityGeneration = eid.Generation,
                .Authority = desc.Authority
            });
        }

        if (m_onNetworkSpawn)
        {
            const bool isOwner = (desc.OwnerPlayer == Network::kInvalidPlayerId);
            m_onNetworkSpawn(
                m_config.WorldId,
                netId,
                eid,
                desc.OwnerPlayer,
                true,
                false,
                isOwner);
        }

        return netId;
    }

    bool SceneReplicationBridge::DespawnReplicatedEntity(
        Scene::Scene& scene,
        Network::SessionRegistry& sessions,
        Network::NetworkId netId)
    {
        if (!netId.IsValid())
        {
            return false;
        }

        if (m_onNetworkDespawn && m_netToScene.find(netId.Pack()) != m_netToScene.end())
        {
            m_onNetworkDespawn(m_config.WorldId, netId);
        }
        m_serverManagedPropCache.erase(netId.Pack());

        auto netIt = m_netToScene.find(netId.Pack());
        if (netIt != m_netToScene.end())
        {
            const Scene::EntityId eid = netIt->second;
            m_netToScene.erase(netIt);
            m_sceneToNet.erase(eid.Pack());
            if (scene.IsAlive(eid))
            {
                // OnSceneEntityDestroyed skips m_sceneToNet since we already erased it, avoiding double work
                scene.DestroyEntity(eid);
            }
        }
        if (auto stIt = m_netToStatic.find(netId.Pack()); stIt != m_netToStatic.end())
        {
            m_staticToNet.erase(stIt->second);
            m_netToStatic.erase(stIt);
        }

        m_inputPipeline.RemoveEntityQueue(netId);
        (void)sessions.UnbindEntity(netId);
        return m_serverRep.DespawnEntity(netId);
    }

    void SceneReplicationBridge::OnSceneEntityDestroyed(
        Scene::EntityId eid,
        Network::SessionRegistry& sessions)
    {
        if (!eid.IsValid())
        {
            return;
        }
        auto sceneIt = m_sceneToNet.find(eid.Pack());
        if (sceneIt == m_sceneToNet.end())
        {
            return;
        }
        const Network::NetworkId netId = sceneIt->second;
        if (m_onNetworkDespawn)
        {
            m_onNetworkDespawn(m_config.WorldId, netId);
        }
        m_serverManagedPropCache.erase(netId.Pack());
        m_sceneToNet.erase(sceneIt);
        m_netToScene.erase(netId.Pack());
        if (auto stIt = m_netToStatic.find(netId.Pack()); stIt != m_netToStatic.end())
        {
            m_staticToNet.erase(stIt->second);
            m_netToStatic.erase(stIt);
        }
        m_inputPipeline.RemoveEntityQueue(netId);
        (void)sessions.UnbindEntity(netId);
        (void)m_serverRep.DespawnEntity(netId);
    }

    void SceneReplicationBridge::OnSceneCleared(Network::SessionRegistry& sessions)
    {
        std::vector<Network::NetworkId> toRemove;
        toRemove.reserve(m_netToScene.size());
        for (const auto& [packed, eid] : m_netToScene)
        {
            (void)eid;
            toRemove.push_back(Network::NetworkId::Unpack(packed));
        }
        m_netToScene.clear();
        m_sceneToNet.clear();
        m_staticToNet.clear();
        m_netToStatic.clear();
        m_serverManagedPropCache.clear();
        for (const Network::NetworkId netId : toRemove)
        {
            if (m_onNetworkDespawn)
            {
                m_onNetworkDespawn(m_config.WorldId, netId);
            }
            m_inputPipeline.RemoveEntityQueue(netId);
            (void)sessions.UnbindEntity(netId);
            (void)m_serverRep.DespawnEntity(netId);
        }
    }

    bool SceneReplicationBridge::RegisterStaticSceneEntities(Scene::Scene& scene, u64 serverTick)
    {
        if (!m_initialized || !m_isServer)
        {
            return false;
        }

        struct PendingStaticEntry
        {
            u64 StaticNetId = 0;
            Scene::EntityId SceneEntity{};
            Scene::NetworkIdentityComponent Identity{};
        };

        const auto netEntities = scene.NetworkIdentities().RawEntities();
        const auto netComps = scene.NetworkIdentities().RawComponents();

        std::vector<PendingStaticEntry> entries;
        entries.reserve(netEntities.size());
        std::unordered_set<u64> seenStaticIds;
        seenStaticIds.reserve(netEntities.size());

        for (usize i = 0; i < netEntities.size(); ++i)
        {
            const Scene::EntityId eid = netEntities[i];
            const Scene::NetworkIdentityComponent& idComp = netComps[i];
            if (!scene.IsAlive(eid) || idComp.Replicated == 0)
            {
                continue;
            }
            if (idComp.StaticNetId == 0)
            {
                Core::Log::Error("Replication", "Static replicated scene entity '" +
                    std::string(scene.GetEntityName(eid)) + "' is missing non-zero static_id.");
                return false;
            }
            if (!seenStaticIds.insert(idComp.StaticNetId).second)
            {
                Core::Log::Error("Replication", "Duplicate static_id (" +
                    std::to_string(idComp.StaticNetId) + ") detected in Scene.");
                return false;
            }
            entries.push_back(PendingStaticEntry{
                .StaticNetId = idComp.StaticNetId,
                .SceneEntity = eid,
                .Identity = idComp
            });
        }

        // Remove stale static bindings that were destroyed or dropped from reloaded scene
        std::vector<u64> staleStaticIds;
        for (const auto& [staticId, oldNetId] : m_staticToNet)
        {
            auto sceneIt = m_netToScene.find(oldNetId.Pack());
            const bool aliveInNewScene = (seenStaticIds.find(staticId) != seenStaticIds.end()) &&
                (sceneIt != m_netToScene.end()) && scene.IsAlive(sceneIt->second);
            if (!aliveInNewScene)
            {
                staleStaticIds.push_back(staticId);
            }
        }
        for (const u64 staleId : staleStaticIds)
        {
            auto stIt = m_staticToNet.find(staleId);
            if (stIt != m_staticToNet.end())
            {
                const Network::NetworkId oldNetId = stIt->second;
                if (auto n2s = m_netToScene.find(oldNetId.Pack()); n2s != m_netToScene.end())
                {
                    m_sceneToNet.erase(n2s->second.Pack());
                    m_netToScene.erase(n2s);
                }
                m_netToStatic.erase(oldNetId.Pack());
                m_staticToNet.erase(stIt);
                (void)m_serverRep.DespawnEntity(oldNetId);
            }
        }

        // Register in ascending StaticNetId order for deterministic snapshot ordering
        std::sort(entries.begin(), entries.end(), [](const PendingStaticEntry& a, const PendingStaticEntry& b) {
            return a.StaticNetId < b.StaticNetId;
        });

        for (const auto& item : entries)
        {
            Network::ReplicatedTransformData repTf{};
            if (const auto* tf = scene.Transforms().TryGet(item.SceneEntity))
            {
                (void)SceneToReplicatedTransform(*tf, repTf);
            }

            if (auto existingIt = m_staticToNet.find(item.StaticNetId); existingIt != m_staticToNet.end())
            {
                const Network::NetworkId existingNetId = existingIt->second;
                auto boundIt = m_netToScene.find(existingNetId.Pack());
                if (boundIt != m_netToScene.end() && boundIt->second == item.SceneEntity)
                {
                    (void)m_serverRep.UpdateEntityTransform(existingNetId, repTf);
                    continue;
                }
            }

            Network::ReplicatedEntitySpawnDesc spawnDesc{};
            spawnDesc.ArchetypeHash = static_cast<u32>((item.StaticNetId ^ (item.StaticNetId >> 32)) | 1u);
            spawnDesc.OwnerSession = Network::kInvalidSessionId;
            spawnDesc.OwnerPlayer = Network::kInvalidPlayerId;
            spawnDesc.WorldId = m_config.WorldId;
            spawnDesc.Authority = item.Identity.Authority <= static_cast<u8>(Network::NetworkAuthority::ClientPredicted)
                ? static_cast<Network::NetworkAuthority>(item.Identity.Authority)
                : Network::NetworkAuthority::Server;
            spawnDesc.Scope = item.Identity.Scope <= static_cast<u8>(Network::ReplicationScope::InitialOnly)
                ? static_cast<Network::ReplicationScope>(item.Identity.Scope)
                : Network::ReplicationScope::AllRelevant;
            spawnDesc.EntityName = std::string(scene.GetEntityName(item.SceneEntity));
            if (spawnDesc.EntityName.size() > Network::ReplicationConstants::kMaxEntityNameBytes)
            {
                spawnDesc.EntityName.resize(Network::ReplicationConstants::kMaxEntityNameBytes);
            }
            spawnDesc.ComponentMask = Network::SnapshotComponentFlags::kTransform;
            spawnDesc.InitialTransform = repTf;
            if (const auto* rb = scene.RigidBodies().TryGet(item.SceneEntity))
            {
                spawnDesc.ComponentMask |= Network::SnapshotComponentFlags::kPhysics;
                spawnDesc.InitialPhysics.LinearVelX = rb->LinearVelocity.x;
                spawnDesc.InitialPhysics.LinearVelY = rb->LinearVelocity.y;
                spawnDesc.InitialPhysics.LinearVelZ = rb->LinearVelocity.z;
                spawnDesc.InitialPhysics.AngularVelX = rb->AngularVelocity.x;
                spawnDesc.InitialPhysics.AngularVelY = rb->AngularVelocity.y;
                spawnDesc.InitialPhysics.AngularVelZ = rb->AngularVelocity.z;
                spawnDesc.InitialPhysics.PhysicsFlags = Network::ReplicatedPhysicsFlags::kRigidBody |
                    (rb->IsAwake == 0u ? Network::ReplicatedPhysicsFlags::kSleeping : 0u);
            }
            if (const auto* cc = scene.CharacterControllers().TryGet(item.SceneEntity))
            {
                spawnDesc.ComponentMask |= Network::SnapshotComponentFlags::kMovement | Network::SnapshotComponentFlags::kPhysics;
                spawnDesc.InitialMovement.VelX = cc->Velocity.x;
                spawnDesc.InitialMovement.VelY = cc->Velocity.y;
                spawnDesc.InitialMovement.VelZ = cc->Velocity.z;
                spawnDesc.InitialPhysics.LinearVelX = cc->Velocity.x;
                spawnDesc.InitialPhysics.LinearVelY = cc->Velocity.y;
                spawnDesc.InitialPhysics.LinearVelZ = cc->Velocity.z;
                spawnDesc.InitialPhysics.PhysicsFlags |= Network::ReplicatedPhysicsFlags::kCharacter |
                    (cc->IsGrounded != 0u ? Network::ReplicatedPhysicsFlags::kGrounded : 0u) |
                    (cc->IsSliding != 0u ? Network::ReplicatedPhysicsFlags::kSliding : 0u);
            }

            const Network::NetworkId netId = m_serverRep.SpawnEntity(spawnDesc, serverTick);
            if (!netId.IsValid())
            {
                return false;
            }

            m_netToScene[netId.Pack()] = item.SceneEntity;
            m_sceneToNet[item.SceneEntity.Pack()] = netId;
            m_staticToNet[item.StaticNetId] = netId;
            m_netToStatic[netId.Pack()] = item.StaticNetId;
        }
        return true;
    }

    void SceneReplicationBridge::OnServerClientAuthenticated(
        const Network::ConnectionInfo& info,
        bool isReconnect,
        Scene::Scene& scene,
        Network::ConnectionManager& net,
        u64 serverTick)
    {
        if (!m_initialized || !m_isServer)
        {
            return;
        }

        if (!m_rpcRouter.IsRegistered(BuiltinRpcIds::kRequestDespawnSelf))
        {
            RegisterBuiltinRpcs(scene, net);
        }

        auto& sessions = net.GetSessionRegistry();
        (void)sessions.AssignSessionWorld(info.Session, m_config.WorldId);
        m_serverRep.RegisterConnection(info.Handle, info.Session, m_config.WorldId);

        if (!m_config.Enabled)
        {
            return;
        }

        if (!isReconnect && m_config.AutoSpawnPlayerEntities)
        {
            const f32 spawnX = static_cast<f32>(m_spawnCounter++) * 3.0f;
            Network::PlayerIdentity identity{};
            std::string entityName = "Player_" + std::to_string(info.AuthenticatedPlayerId);
            if (sessions.TryGetPlayer(info.AuthenticatedPlayerId, identity) && !identity.DisplayName.empty())
            {
                entityName = identity.DisplayName;
                if (entityName.size() > Network::ReplicationConstants::kMaxEntityNameBytes)
                {
                    entityName.resize(Network::ReplicationConstants::kMaxEntityNameBytes);
                }
            }

            Network::ReplicatedEntitySpawnDesc spawnDesc{};
            spawnDesc.ArchetypeHash = 1;
            spawnDesc.OwnerSession = info.Session;
            spawnDesc.OwnerPlayer = info.AuthenticatedPlayerId;
            spawnDesc.WorldId = m_config.WorldId;
            spawnDesc.Authority = Network::NetworkAuthority::ClientPredicted;
            spawnDesc.Scope = Network::ReplicationScope::AllRelevant;
            spawnDesc.EntityName = entityName;
            spawnDesc.ComponentMask = Network::SnapshotComponentFlags::kTransform | Network::SnapshotComponentFlags::kMovement;
            if (m_config.UsePhysicsCharacterForPlayers)
            {
                spawnDesc.ComponentMask |= Network::SnapshotComponentFlags::kPhysics;
                spawnDesc.InitialPhysics.PhysicsFlags = Network::ReplicatedPhysicsFlags::kCharacter;
            }
            spawnDesc.InitialTransform.PosX = spawnX;
            spawnDesc.InitialTransform.PosY = 0.0f;
            spawnDesc.InitialTransform.PosZ = 0.0f;

            const Network::NetworkId netId = SpawnReplicatedEntity(scene, sessions, spawnDesc, serverTick);
            if (netId.IsValid())
            {
                if (m_config.UsePhysicsCharacterForPlayers)
                {
                    const Scene::EntityId playerEid = ResolveSceneEntity(netId);
                    if (scene.IsAlive(playerEid) && scene.CharacterControllers().TryGet(playerEid) == nullptr)
                    {
                        Scene::CharacterControllerComponent cc{};
                        cc.Radius = 0.35f;
                        cc.Height = 1.8f;
                        cc.Mass = 70.0f;
                        cc.MaxPushStrength = 250.0f;
                        cc.DirtyFlags = static_cast<u8>(Scene::CharacterControllerDirtyFlags::All);
                        scene.CharacterControllers().EmplaceOrReplace(playerEid, cc);
                    }
                }
                m_serverRep.SetConnectionViewPosition(info.Handle, spawnX, 0.0f, 0.0f);
                Core::Log::Info("Replication", "Spawned authoritative entity NetId=(" +
                    std::to_string(netId.Index) + ":" + std::to_string(netId.Generation) +
                    ") for session=" + info.Session.ToHexString() +
                    " playerId=" + std::to_string(info.AuthenticatedPlayerId));
            }
        }
        else if (isReconnect)
        {
            const auto owned = m_serverRep.GetEntitiesForSession(info.Session);
            if (!owned.empty())
            {
                Network::ServerReplicatedEntity ent{};
                if (m_serverRep.TryGetEntity(owned.front(), ent))
                {
                    m_serverRep.SetConnectionViewPosition(
                        info.Handle,
                        ent.Transform.PosX,
                        ent.Transform.PosY,
                        ent.Transform.PosZ);
                }
            }
            m_serverRep.RequestFullResyncForConnection(info.Handle);
        }

        if (!m_serverManagedPropCache.empty())
        {
            m_pendingPropReplayConns.push_back(info.Handle);
        }
    }

    void SceneReplicationBridge::OnServerSessionClosed(
        Network::SessionId sessionId,
        Scene::Scene& scene,
        Network::ConnectionManager& net)
    {
        if (!m_initialized || !m_isServer || !sessionId.IsValid())
        {
            return;
        }
        const auto owned = m_serverRep.GetEntitiesForSession(sessionId);
        auto& sessions = net.GetSessionRegistry();
        for (const Network::NetworkId netId : owned)
        {
            Core::Log::Info("Replication", "Despawning entity NetId=(" +
                std::to_string(netId.Index) + ":" + std::to_string(netId.Generation) +
                ") for closed session=" + sessionId.ToHexString());
            (void)DespawnReplicatedEntity(scene, sessions, netId);
        }
    }

    void SceneReplicationBridge::ProcessServerInputs(
        f32 fixedDt,
        u64 serverTick,
        Scene::Scene& scene,
        Physics::PhysicsWorld* physics)
    {
        if (!m_initialized || !m_isServer || !m_config.Enabled)
        {
            return;
        }
        m_lastInputProcessedTick = serverTick;

        for (auto& [packedNetId, eid] : m_netToScene)
        {
            const Network::NetworkId netId = Network::NetworkId::Unpack(packedNetId);
            if (!scene.IsAlive(eid))
            {
                continue;
            }

            if (m_inputPipeline.DrainCommands(netId, m_scratchInputs, 8) == 0)
            {
                continue;
            }

            auto* tf = scene.Transforms().TryGet(eid);
            if (tf == nullptr)
            {
                continue;
            }

            Network::ServerReplicatedEntity curState{};
            (void)m_serverRep.TryGetEntity(netId, curState);
            Network::ReplicatedMovementData mov = curState.Movement;

            for (const Network::PlayerInputCommand& cmd : m_scratchInputs)
            {
                ApplyCharacterInputCommand(
                    cmd,
                    fixedDt,
                    m_config.PlayerMoveSpeed,
                    m_config.PlayerJumpSpeed,
                    scene,
                    eid,
                    physics,
                    mov);
            }
            (void)m_serverRep.UpdateEntityMovement(netId, mov);
        }
    }

    void SceneReplicationBridge::StepServerTick(
        f32 fixedDt,
        u64 serverTick,
        u32 simulationTickRateHz,
        Scene::Scene& scene,
        Physics::PhysicsWorld* physics,
        Network::ConnectionManager& net)
    {
        if (!m_initialized || !m_isServer || !m_config.Enabled)
        {
            return;
        }

        const u64 repStartUs = NowSteadyUs();

        // 1. Drain inputs if caller did not already invoke ProcessServerInputs before PhysicsWorld::StepSimulation
        if (m_lastInputProcessedTick != serverTick)
        {
            ProcessServerInputs(fixedDt, serverTick, scene, physics);
        }

        // 2. Sync authoritative Scene & Jolt Physics state -> ReplicationManager
        for (auto& [packedNetId, eid] : m_netToScene)
        {
            const Network::NetworkId netId = Network::NetworkId::Unpack(packedNetId);
            if (!scene.IsAlive(eid))
            {
                continue;
            }

            if (const auto* tf = scene.Transforms().TryGet(eid))
            {
                Network::ReplicatedTransformData repTf{};
                if (SceneToReplicatedTransform(*tf, repTf))
                {
                    (void)m_serverRep.UpdateEntityTransform(netId, repTf);
                }
            }

            if (const auto* cc = scene.CharacterControllers().TryGet(eid))
            {
                Network::ServerReplicatedEntity curState{};
                (void)m_serverRep.TryGetEntity(netId, curState);

                Network::ReplicatedMovementData mov = curState.Movement;
                mov.VelX = cc->Velocity.x;
                mov.VelY = cc->Velocity.y;
                mov.VelZ = cc->Velocity.z;
                if (cc->IsGrounded != 0u)
                {
                    mov.MovementFlags |= 4u;
                }
                else
                {
                    mov.MovementFlags &= static_cast<u8>(~4u);
                }
                (void)m_serverRep.UpdateEntityMovement(netId, mov);

                Network::ReplicatedPhysicsStateData phys{};
                phys.LinearVelX = cc->Velocity.x;
                phys.LinearVelY = cc->Velocity.y;
                phys.LinearVelZ = cc->Velocity.z;
                phys.PhysicsFlags = Network::ReplicatedPhysicsFlags::kCharacter |
                    (cc->IsGrounded != 0u ? Network::ReplicatedPhysicsFlags::kGrounded : 0u) |
                    (cc->IsSliding != 0u ? Network::ReplicatedPhysicsFlags::kSliding : 0u);
                (void)m_serverRep.UpdateEntityPhysics(netId, phys);
            }
            else if (const auto* rb = scene.RigidBodies().TryGet(eid))
            {
                Network::ReplicatedPhysicsStateData phys{};
                phys.LinearVelX = rb->LinearVelocity.x;
                phys.LinearVelY = rb->LinearVelocity.y;
                phys.LinearVelZ = rb->LinearVelocity.z;
                phys.AngularVelX = rb->AngularVelocity.x;
                phys.AngularVelY = rb->AngularVelocity.y;
                phys.AngularVelZ = rb->AngularVelocity.z;
                phys.PhysicsFlags = Network::ReplicatedPhysicsFlags::kRigidBody |
                    (rb->IsAwake == 0u ? Network::ReplicatedPhysicsFlags::kSleeping : 0u);
                (void)m_serverRep.UpdateEntityPhysics(netId, phys);
            }
        }

        // Update viewer positions for distance-based interest management
        const auto conns = net.GetConnections();
        for (const auto connHandle : conns)
        {
            Network::ConnectionInfo ci{};
            if (!net.TryGetConnectionInfo(connHandle, ci) ||
                ci.State != Network::ConnectionState::Connected ||
                !ci.Session.IsValid())
            {
                continue;
            }
            const auto owned = m_serverRep.GetEntitiesForSession(ci.Session);
            if (!owned.empty())
            {
                Network::ServerReplicatedEntity ent{};
                if (m_serverRep.TryGetEntity(owned.front(), ent))
                {
                    m_serverRep.SetConnectionViewPosition(
                        connHandle,
                        ent.Transform.PosX,
                        ent.Transform.PosY,
                        ent.Transform.PosZ);
                }
            }
        }

        // 2. Snapshot / delta emission at configured replication cadence
        const u32 repHz = (std::max)(m_config.ReplicationRateHz, 1u);
        const u32 simHz = (std::max)(simulationTickRateHz, 1u);
        const u32 intervalTicks = (std::max)(simHz / repHz, 1u);

        if ((serverTick % intervalTicks) == 0)
        {
            std::optional<std::vector<u8>> singleSnapshotBytes;
            std::vector<Network::KeyframeChunkPacket> keyframeChunks;
            for (const auto connHandle : conns)
            {
                Network::ConnectionInfo ci{};
                if (!net.TryGetConnectionInfo(connHandle, ci) ||
                    ci.State != Network::ConnectionState::Connected)
                {
                    continue;
                }

                auto pktOpt = m_serverRep.BuildSnapshotForConnection(connHandle, serverTick);
                if (!pktOpt.has_value())
                {
                    continue;
                }

                if (!m_serverRep.PrepareOutgoingSnapshot(*pktOpt, singleSnapshotBytes, keyframeChunks))
                {
                    continue;
                }

                if (singleSnapshotBytes.has_value())
                {
                    const Network::NetDelivery delivery = pktOpt->IsKeyframe
                        ? Network::NetDelivery::ReliableOrdered
                        : Network::NetDelivery::UnreliableSequenced;
                    (void)net.SendPacket(
                        connHandle,
                        Network::MessageCategory::Replication,
                        static_cast<u8>(Network::ReplicationMessageSubType::ServerSnapshot),
                        *singleSnapshotBytes,
                        delivery,
                        Network::NetChannel::Snapshot);
                }
                else
                {
                    for (const auto& chunk : keyframeChunks)
                    {
                        Network::BitWriter chunkWriter(Network::ReplicationConstants::kMaxKeyframeChunkPayloadBytes + 128);
                        if (Network::ReplicationCodec::WriteKeyframeChunk(chunkWriter, chunk))
                        {
                            (void)net.SendPacket(
                                connHandle,
                                Network::MessageCategory::Replication,
                                static_cast<u8>(Network::ReplicationMessageSubType::ServerKeyframeChunk),
                                chunkWriter.GetData(),
                                Network::NetDelivery::ReliableOrdered,
                                Network::NetChannel::Snapshot);
                        }
                    }
                }
            }
        }

        if (!m_pendingPropReplayConns.empty())
        {
            for (const Network::ConnectionHandle connHandle : m_pendingPropReplayConns)
            {
                Network::ConnectionInfo ci{};
                if (!net.TryGetConnectionInfo(connHandle, ci))
                {
                    continue;
                }
                for (const auto& [packedNetId, behaviourMap] : m_serverManagedPropCache)
                {
                    const Network::NetworkId targetNetId = Network::NetworkId::Unpack(packedNetId);
                    Network::ServerReplicatedEntity ent{};
                    if (!m_serverRep.TryGetEntity(targetNetId, ent))
                    {
                        continue;
                    }
                    for (const auto& [behaviourHash, cachedBytes] : behaviourMap)
                    {
                        std::vector<Network::ManagedPropertyEntry> allProps;
                        if (!DecodeManagedPropsBytes(cachedBytes, allProps) || allProps.empty())
                        {
                            continue;
                        }
                        usize offset = 0;
                        while (offset < allProps.size())
                        {
                            const usize batchCount = (std::min)(
                                Network::ReplicationConstants::kMaxManagedPropertiesPerSync,
                                allProps.size() - offset);
                            Network::ManagedPropertySyncPacket pkt{};
                            pkt.WireVersion = Network::ReplicationConstants::kManagedWireVersion;
                            pkt.WorldId = m_config.WorldId;
                            pkt.TargetNetId = targetNetId;
                            pkt.BehaviourTypeHash = behaviourHash;
                            pkt.Properties.assign(allProps.begin() + offset, allProps.begin() + offset + batchCount);

                            Network::BitWriter w(256);
                            if (Network::ReplicationCodec::WriteManagedPropertySync(w, pkt))
                            {
                                (void)net.SendPacket(
                                    connHandle,
                                    Network::MessageCategory::Replication,
                                    static_cast<u8>(Network::ReplicationMessageSubType::ManagedPropertySync),
                                    w.GetData(),
                                    Network::NetDelivery::ReliableOrdered,
                                    Network::NetChannel::Snapshot);
                            }
                            offset += batchCount;
                        }
                    }
                }
            }
            m_pendingPropReplayConns.clear();
        }

        m_serverRep.RecordReplicationDurationUs(NowSteadyUs() - repStartUs);
    }

    void SceneReplicationBridge::HandleNetworkMessage(
        const Network::NetworkMessageEvent& ev,
        Scene::Scene& scene,
        Network::ConnectionManager& net)
    {
        if (!m_initialized)
        {
            return;
        }

        const u64 nowMs = NowSteadyMs();
        if (ev.Header.Category == Network::MessageCategory::Rpc)
        {
            if (!m_rpcRouter.IsRegistered(BuiltinRpcIds::kManagedClientRpc))
            {
                RegisterBuiltinRpcs(scene, net);
            }
            (void)m_rpcRouter.ProcessIncomingRpc(
                ev.Connection,
                net.GetSessionRegistry(),
                m_config.WorldId,
                ev.Payload,
                nowMs);
            return;
        }

        if (ev.Header.Category != Network::MessageCategory::Replication)
        {
            return;
        }

        const auto subType = static_cast<Network::ReplicationMessageSubType>(ev.Header.SubType);
        if (m_isServer)
        {
            Network::BitReader reader(ev.Payload);
            switch (subType)
            {
            case Network::ReplicationMessageSubType::ClientSnapshotAck:
            {
                Network::SnapshotAckPacket ack{};
                if (Network::ReplicationCodec::ReadSnapshotAck(reader, ack))
                {
                    (void)m_serverRep.HandleSnapshotAck(ev.Connection, ack);
                }
                break;
            }
            case Network::ReplicationMessageSubType::ClientRequestResync:
            {
                Network::ResyncRequestPacket req{};
                if (Network::ReplicationCodec::ReadResyncRequest(reader, req))
                {
                    (void)m_serverRep.HandleResyncRequest(ev.Connection, req);
                }
                break;
            }
            case Network::ReplicationMessageSubType::ClientInputCommand:
            {
                Network::PlayerInputCommand cmd{};
                if (cmd.Read(reader))
                {
                    (void)m_inputPipeline.SubmitCommand(
                        ev.Connection,
                        net.GetSessionRegistry(),
                        m_config.WorldId,
                        cmd,
                        nowMs);
                }
                else
                {
                    Network::PlayerInputCommand invalid{};
                    (void)m_inputPipeline.SubmitCommand(
                        ev.Connection,
                        net.GetSessionRegistry(),
                        m_config.WorldId,
                        invalid,
                        nowMs);
                }
                break;
            }
            default:
                break;
            }
        }
        else if (subType == Network::ReplicationMessageSubType::ServerSnapshot)
        {
            ApplyServerSnapshotOnClient(ev, scene, net);
        }
        else if (subType == Network::ReplicationMessageSubType::ServerKeyframeChunk)
        {
            ApplyServerKeyframeChunkOnClient(ev, scene, net, nowMs);
        }
        else if (subType == Network::ReplicationMessageSubType::ManagedPropertySync)
        {
            Network::BitReader reader(ev.Payload);
            Network::ManagedPropertySyncPacket pkt{};
            if (Network::ReplicationCodec::ReadManagedPropertySync(reader, pkt))
            {
                const Network::WorldInstanceId activeW =
                    (m_activeClientWorldId != Network::kInvalidWorldInstanceId) ? m_activeClientWorldId : m_config.WorldId;
                if (pkt.WorldId == activeW)
                {
                    Network::ClientReplicatedEntity clientEnt{};
                    if (m_clientApplier.TryGetEntity(pkt.TargetNetId, clientEnt) && m_onManagedPropertySync)
                    {
                        std::vector<u8> propsBytes;
                        if (EncodeManagedPropsBytes(pkt.Properties, propsBytes))
                        {
                            (void)m_onManagedPropertySync(pkt.WorldId, pkt.TargetNetId, pkt.BehaviourTypeHash, propsBytes);
                        }
                    }
                }
            }
        }
    }

    void SceneReplicationBridge::SendClientResyncRequest(
        Network::ConnectionHandle serverConn,
        Network::ConnectionManager& net,
        const Network::ResyncRequestPacket& req)
    {
        Network::BitWriter w(32);
        if (Network::ReplicationCodec::WriteResyncRequest(w, req))
        {
            (void)net.SendPacket(
                serverConn,
                Network::MessageCategory::Replication,
                static_cast<u8>(Network::ReplicationMessageSubType::ClientRequestResync),
                w.GetData(),
                Network::NetDelivery::ReliableOrdered,
                Network::NetChannel::Snapshot);
        }
    }

    void SceneReplicationBridge::CommitClientApplierOutcome(
        bool applied,
        const std::vector<Network::ClientReplicationEvent>& events,
        const std::optional<Network::SnapshotAckPacket>& ackOpt,
        const std::optional<Network::ResyncRequestPacket>& resyncOpt,
        Network::ConnectionHandle serverConn,
        Scene::Scene& scene,
        Network::ConnectionManager& net)
    {
        if (applied)
        {
            const Network::WorldInstanceId newWorldId = m_clientApplier.GetActiveWorldId();
            if (m_activeClientWorldId != Network::kInvalidWorldInstanceId &&
                newWorldId != Network::kInvalidWorldInstanceId &&
                newWorldId != m_activeClientWorldId)
            {
                m_clientInputHistory.Reset();
                m_remoteInterp.Clear();
                m_visualCorrectionOffset = Math::Vec3::Zero();
                m_localOwnedNetId = Network::kInvalidNetworkId;
                m_nextClientInputSeq = 1;
                m_clientPredictionTick = 1;
            }
            if (newWorldId != Network::kInvalidWorldInstanceId)
            {
                m_activeClientWorldId = newWorldId;
            }

            if (m_localPlayerId == Network::kInvalidPlayerId && serverConn.IsValid())
            {
                Network::ConnectionInfo ci{};
                if (net.TryGetConnectionInfo(serverConn, ci) && ci.AuthenticatedPlayerId != Network::kInvalidPlayerId)
                {
                    m_localPlayerId = ci.AuthenticatedPlayerId;
                }
            }

            for (const auto& repEv : events)
            {
                const u64 packedNet = repEv.NetId.Pack();
                if (repEv.Kind == Network::ReplicationOpKind::Despawn)
                {
                    m_remoteInterp.RemoveEntity(repEv.NetId);
                    if (repEv.NetId == m_localOwnedNetId)
                    {
                        m_localOwnedNetId = Network::kInvalidNetworkId;
                        m_clientInputHistory.ClearEntries();
                        m_visualCorrectionOffset = Math::Vec3::Zero();
                    }
                    auto it = m_netToScene.find(packedNet);
                    if (it != m_netToScene.end())
                    {
                        if (m_onNetworkDespawn)
                        {
                            const Network::WorldInstanceId activeW =
                                (m_activeClientWorldId != Network::kInvalidWorldInstanceId) ? m_activeClientWorldId : m_config.WorldId;
                            m_onNetworkDespawn(activeW, repEv.NetId);
                        }
                        const Scene::EntityId eid = it->second;
                        m_netToScene.erase(it);
                        m_sceneToNet.erase(eid.Pack());
                        if (scene.IsAlive(eid))
                        {
                            scene.DestroyEntity(eid);
                        }
                    }
                    continue;
                }

                Network::ClientReplicatedEntity clientEnt{};
                if (!m_clientApplier.TryGetEntity(repEv.NetId, clientEnt))
                {
                    continue;
                }

                const bool isNewSpawn = (m_netToScene.find(packedNet) == m_netToScene.end());
                Scene::EntityId eid = Scene::EntityId::Invalid();
                auto mapIt = m_netToScene.find(packedNet);
                if (mapIt != m_netToScene.end() && scene.IsAlive(mapIt->second))
                {
                    eid = mapIt->second;
                }
                else
                {
                    eid = scene.CreateEntity(clientEnt.EntityName.empty() ? "RepEntity" : clientEnt.EntityName);
                    m_netToScene[packedNet] = eid;
                    m_sceneToNet[eid.Pack()] = repEv.NetId;
                }

                // Identify locally owned predicted entity
                if (m_localPlayerId != Network::kInvalidPlayerId &&
                    clientEnt.OwnerPlayer == m_localPlayerId &&
                    clientEnt.Authority == Network::NetworkAuthority::ClientPredicted)
                {
                    m_localOwnedNetId = repEv.NetId;
                }

                if (isNewSpawn && m_onNetworkSpawn)
                {
                    const Network::WorldInstanceId activeW =
                        (m_activeClientWorldId != Network::kInvalidWorldInstanceId) ? m_activeClientWorldId : m_config.WorldId;
                    const bool isOwner = (m_localPlayerId != Network::kInvalidPlayerId && clientEnt.OwnerPlayer == m_localPlayerId);
                    m_onNetworkSpawn(
                        activeW,
                        repEv.NetId,
                        eid,
                        clientEnt.OwnerPlayer,
                        false,
                        true,
                        isOwner);
                }

                const bool isLocalOwned = (repEv.NetId == m_localOwnedNetId);
                if (isLocalOwned &&
                    m_config.UsePhysicsCharacterForPlayers &&
                    m_clientPhysics != nullptr &&
                    scene.CharacterControllers().TryGet(eid) == nullptr)
                {
                    Scene::CharacterControllerComponent cc{};
                    cc.Radius = 0.35f;
                    cc.Height = 1.8f;
                    cc.Mass = 70.0f;
                    cc.MaxPushStrength = 250.0f;
                    cc.DirtyFlags = static_cast<u8>(Scene::CharacterControllerDirtyFlags::All);
                    scene.CharacterControllers().EmplaceOrReplace(eid, cc);
                }

                if ((clientEnt.ComponentMask & Network::SnapshotComponentFlags::kPhysics) != 0)
                {
                    if (auto* rb = scene.RigidBodies().TryGet(eid))
                    {
                        rb->LinearVelocity = Math::Vec3{
                            clientEnt.Physics.LinearVelX,
                            clientEnt.Physics.LinearVelY,
                            clientEnt.Physics.LinearVelZ
                        };
                        rb->AngularVelocity = Math::Vec3{
                            clientEnt.Physics.AngularVelX,
                            clientEnt.Physics.AngularVelY,
                            clientEnt.Physics.AngularVelZ
                        };
                        rb->IsAwake = ((clientEnt.Physics.PhysicsFlags & Network::ReplicatedPhysicsFlags::kSleeping) == 0u) ? 1u : 0u;
                    }
                    if (auto* cc = scene.CharacterControllers().TryGet(eid))
                    {
                        cc->Velocity = Math::Vec3{
                            clientEnt.Physics.LinearVelX,
                            clientEnt.Physics.LinearVelY,
                            clientEnt.Physics.LinearVelZ
                        };
                        cc->IsGrounded = ((clientEnt.Physics.PhysicsFlags & Network::ReplicatedPhysicsFlags::kGrounded) != 0u) ? 1u : 0u;
                        cc->IsSliding = ((clientEnt.Physics.PhysicsFlags & Network::ReplicatedPhysicsFlags::kSliding) != 0u) ? 1u : 0u;
                    }
                }

                if ((clientEnt.ComponentMask & Network::SnapshotComponentFlags::kTransform) == 0)
                {
                    continue;
                }

                if (isLocalOwned &&
                    m_predictionConfig.EnablePrediction &&
                    m_clientInputHistory.GetLastPushedSequence() > 0)
                {
                    // Server Reconciliation: acknowledge confirmed inputs and replay unconfirmed inputs
                    auto* tf = scene.Transforms().TryGet(eid);
                    if (tf == nullptr)
                    {
                        continue;
                    }

                    const Math::Vec3 preLogicalPos = m_logicalPredictedPos;
                    const Math::Vec3 preVisualPos = tf->Position;

                    (void)m_clientInputHistory.AcknowledgeUpTo(clientEnt.Movement.LastProcessedInputSeq);

                    (void)ReplicatedToSceneTransform(clientEnt.Transform, *tf);
                    if (m_clientPhysics != nullptr && scene.CharacterControllers().TryGet(eid) != nullptr)
                    {
                        (void)m_clientPhysics->TeleportCharacter(scene, eid, tf->Position);
                        (void)m_clientPhysics->SetCharacterVelocity(scene, eid, Math::Vec3{
                            clientEnt.Movement.VelX,
                            clientEnt.Movement.VelY,
                            clientEnt.Movement.VelZ
                        });
                    }

                    m_clientInputHistory.CopyUnacknowledged(m_scratchHistory);
                    Network::ReplicatedMovementData replayMov = clientEnt.Movement;
                    for (const auto& hist : m_scratchHistory)
                    {
                        ApplyCharacterInputCommand(
                            hist.Command,
                            hist.FixedDt,
                            m_config.PlayerMoveSpeed,
                            m_config.PlayerJumpSpeed,
                            scene,
                            eid,
                            m_clientPhysics,
                            replayMov);
                        if (m_clientPhysics != nullptr && scene.CharacterControllers().TryGet(eid) != nullptr)
                        {
                            (void)m_clientPhysics->StepSimulation(hist.FixedDt, scene);
                        }
                    }

                    const Math::Vec3 reconciledPos = tf->Position;
                    m_logicalPredictedPos = reconciledPos;

                    const Math::Vec3 diff = reconciledPos - preLogicalPos;
                    const f32 errLen = Math::Length(diff);
                    ++m_predictionStats.ReconciliationsPerformed;
                    m_predictionStats.InputsReplayed += m_scratchHistory.size();

                    if (errLen > m_predictionConfig.ReconciliationIgnoreThreshold)
                    {
                        ++m_predictionStats.CorrectionsApplied;
                        m_predictionStats.MaxCorrectionDistance = (std::max)(m_predictionStats.MaxCorrectionDistance, errLen);
                        if (errLen >= m_predictionConfig.ReconciliationSnapThreshold)
                        {
                            m_visualCorrectionOffset = Math::Vec3::Zero();
                            ++m_predictionStats.LargeSnaps;
                        }
                        else
                        {
                            m_visualCorrectionOffset = preVisualPos - reconciledPos;
                        }
                    }
                    tf->Position = m_logicalPredictedPos + m_visualCorrectionOffset;
                }
                else
                {
                    if (!isLocalOwned)
                    {
                        m_remoteInterp.PushSample(
                            repEv.NetId,
                            m_lastClientSnapshotTick,
                            m_clientClockSec,
                            clientEnt.Transform);
                    }

                    if (isLocalOwned || isNewSpawn || !m_clientInterpolationDriven || !m_predictionConfig.EnableRemoteInterpolation)
                    {
                        if (auto* tf = scene.Transforms().TryGet(eid))
                        {
                            (void)ReplicatedToSceneTransform(clientEnt.Transform, *tf);
                            if (isLocalOwned)
                            {
                                m_logicalPredictedPos = tf->Position;
                            }
                        }
                    }
                }
            }
        }

        if (ackOpt.has_value())
        {
            Network::BitWriter w(32);
            if (Network::ReplicationCodec::WriteSnapshotAck(w, *ackOpt))
            {
                (void)net.SendPacket(
                    serverConn,
                    Network::MessageCategory::Replication,
                    static_cast<u8>(Network::ReplicationMessageSubType::ClientSnapshotAck),
                    w.GetData(),
                    Network::NetDelivery::ReliableOrdered,
                    Network::NetChannel::Snapshot);
            }
        }
        else if (resyncOpt.has_value())
        {
            SendClientResyncRequest(serverConn, net, *resyncOpt);
        }
    }

    void SceneReplicationBridge::ApplyServerSnapshotOnClient(
        const Network::NetworkMessageEvent& ev,
        Scene::Scene& scene,
        Network::ConnectionManager& net)
    {
        Network::BitReader reader(ev.Payload);
        Network::SnapshotPacket pkt{};
        if (!Network::ReplicationCodec::ReadSnapshotPacket(reader, pkt))
        {
            const Network::ResyncRequestPacket req{
                .LastAppliedSequence = m_clientApplier.GetLastAppliedSequence(),
                .Reason = Network::ResyncReason::DecodeFailure,
                .BaselineEpoch = m_clientApplier.GetActiveBaselineEpoch() != 0
                    ? m_clientApplier.GetActiveBaselineEpoch()
                    : 1u
            };
            SendClientResyncRequest(ev.Connection, net, req);
            return;
        }

        std::vector<Network::ClientReplicationEvent> events;
        std::optional<Network::SnapshotAckPacket> ackOpt;
        std::optional<Network::ResyncRequestPacket> resyncOpt;
        m_lastClientSnapshotTick = pkt.ServerTick;
        const bool ok = m_clientApplier.ApplySnapshotPacket(pkt, events, ackOpt, resyncOpt);
        CommitClientApplierOutcome(ok, events, ackOpt, resyncOpt, ev.Connection, scene, net);
    }

    void SceneReplicationBridge::ApplyServerKeyframeChunkOnClient(
        const Network::NetworkMessageEvent& ev,
        Scene::Scene& scene,
        Network::ConnectionManager& net,
        u64 nowMs)
    {
        Network::BitReader reader(ev.Payload);
        Network::KeyframeChunkPacket chunk{};
        if (!Network::ReplicationCodec::ReadKeyframeChunk(reader, chunk))
        {
            m_clientApplier.CancelPendingKeyframeTransfer();
            const Network::ResyncRequestPacket req{
                .LastAppliedSequence = m_clientApplier.GetLastAppliedSequence(),
                .Reason = Network::ResyncReason::DecodeFailure,
                .BaselineEpoch = m_clientApplier.GetActiveBaselineEpoch() != 0
                    ? m_clientApplier.GetActiveBaselineEpoch()
                    : 1u
            };
            SendClientResyncRequest(ev.Connection, net, req);
            return;
        }

        std::vector<Network::ClientReplicationEvent> events;
        std::optional<Network::SnapshotAckPacket> ackOpt;
        std::optional<Network::ResyncRequestPacket> resyncOpt;
        m_lastClientSnapshotTick = chunk.ServerTick;
        const bool ok = m_clientApplier.ProcessKeyframeChunk(chunk, nowMs, events, ackOpt, resyncOpt);
        CommitClientApplierOutcome(ok, events, ackOpt, resyncOpt, ev.Connection, scene, net);
    }

    void SceneReplicationBridge::TickClientMaintenance(
        Network::ConnectionManager& net,
        Network::ConnectionHandle serverConnection)
    {
        if (!m_initialized || m_isServer || !serverConnection.IsValid())
        {
            return;
        }
        std::optional<Network::ResyncRequestPacket> resyncOpt;
        if (m_clientApplier.TickKeyframeTimeouts(NowSteadyMs(), resyncOpt) && resyncOpt.has_value())
        {
            SendClientResyncRequest(serverConnection, net, *resyncOpt);
        }
    }

    bool SceneReplicationBridge::ClientSendInput(
        Network::ConnectionManager& net,
        Network::ConnectionHandle serverConnection,
        const Network::PlayerInputCommand& cmd)
    {
        Network::BitWriter w(128);
        if (!cmd.Write(w))
        {
            return false;
        }
        return net.SendPacket(
            serverConnection,
            Network::MessageCategory::Replication,
            static_cast<u8>(Network::ReplicationMessageSubType::ClientInputCommand),
            w.GetData(),
            Network::NetDelivery::UnreliableSequenced,
            Network::NetChannel::GameplayEvents).Succeeded();
    }

    void SceneReplicationBridge::ConfigurePrediction(const ClientPredictionConfig& config) noexcept
    {
        m_predictionConfig = config;
        m_clientInputHistory.SetCapacity(config.InputHistoryCapacity);
        m_remoteInterp.SetInterpolationDelaySec(config.InterpolationDelaySec);
    }

    void SceneReplicationBridge::PrepareClientForReconnect() noexcept
    {
        m_clientApplier.PrepareForReconnect();
        m_clientInputHistory.Reset();
        m_remoteInterp.Clear();
        m_visualCorrectionOffset = Math::Vec3::Zero();
        m_nextClientInputSeq = 1;
        m_clientPredictionTick = 1;
    }

    bool SceneReplicationBridge::ClientPredictAndSendInput(
        f32 fixedDt,
        const Network::PlayerInputCommand& partialCmd,
        Scene::Scene& scene,
        Physics::PhysicsWorld* clientPhysics,
        Network::ConnectionManager& net,
        Network::ConnectionHandle serverConnection)
    {
        if (!m_initialized || m_isServer || !serverConnection.IsValid() || fixedDt <= 0.0f)
        {
            return false;
        }

        m_clientPhysics = clientPhysics;
        Network::NetworkId targetNetId = partialCmd.TargetEntity.IsValid()
            ? partialCmd.TargetEntity
            : m_localOwnedNetId;
        if (!targetNetId.IsValid())
        {
            return false;
        }

        const Scene::EntityId eid = ResolveSceneEntity(targetNetId);
        if (!scene.IsAlive(eid))
        {
            return false;
        }
        auto* tf = scene.Transforms().TryGet(eid);
        if (tf == nullptr)
        {
            return false;
        }

        Network::PlayerInputCommand cmd = partialCmd;
        cmd.TargetEntity = targetNetId;
        cmd.Sequence = (partialCmd.Sequence != 0) ? partialCmd.Sequence : m_nextClientInputSeq++;
        if (cmd.Sequence >= m_nextClientInputSeq)
        {
            m_nextClientInputSeq = cmd.Sequence + 1;
        }
        cmd.ClientTick = (partialCmd.ClientTick != 0) ? partialCmd.ClientTick : m_clientPredictionTick++;

        if (!ClientSendInput(net, serverConnection, cmd))
        {
            return false;
        }

        if (!m_predictionConfig.EnablePrediction)
        {
            return true;
        }

        // Simulate locally from logical predicted position (without visual smoothing offset)
        tf->Position = m_logicalPredictedPos;
        Network::ReplicatedMovementData mov{};
        ApplyCharacterInputCommand(
            cmd,
            fixedDt,
            m_config.PlayerMoveSpeed,
            m_config.PlayerJumpSpeed,
            scene,
            eid,
            clientPhysics,
            mov);

        if (clientPhysics != nullptr && scene.CharacterControllers().TryGet(eid) != nullptr)
        {
            (void)clientPhysics->StepSimulation(fixedDt, scene);
        }

        m_logicalPredictedPos = tf->Position;
        tf->Position = m_logicalPredictedPos + m_visualCorrectionOffset;

        Network::ClientInputHistoryEntry hist{};
        hist.Command = cmd;
        hist.FixedDt = fixedDt;
        hist.PredictedPosX = m_logicalPredictedPos.x;
        hist.PredictedPosY = m_logicalPredictedPos.y;
        hist.PredictedPosZ = m_logicalPredictedPos.z;
        hist.PredictedVelX = mov.VelX;
        hist.PredictedVelY = mov.VelY;
        hist.PredictedVelZ = mov.VelZ;
        (void)m_clientInputHistory.Push(hist);
        ++m_predictionStats.PredictedInputsSubmitted;
        return true;
    }

    void SceneReplicationBridge::TickClientPredictionAndInterpolation(
        f32 frameDt,
        Scene::Scene& scene,
        Network::ConnectionManager& net,
        Network::ConnectionHandle serverConnection,
        Physics::PhysicsWorld* clientPhysics)
    {
        if (!m_initialized || m_isServer)
        {
            return;
        }

        m_clientPhysics = clientPhysics;
        m_clientInterpolationDriven = true;
        const f32 safeDt = (std::max)(0.0f, frameDt);
        m_clientClockSec += static_cast<f64>(safeDt);

        TickClientMaintenance(net, serverConnection);

        // Exponential decay of visual reconciliation smoothing offset
        if (m_predictionConfig.VisualSmoothingRate > 0.0f && safeDt > 0.0f)
        {
            const f32 decay = std::exp(-m_predictionConfig.VisualSmoothingRate * safeDt);
            m_visualCorrectionOffset = m_visualCorrectionOffset * decay;
            if (Math::Length(m_visualCorrectionOffset) < 1e-4f)
            {
                m_visualCorrectionOffset = Math::Vec3::Zero();
            }
        }
        else if (m_predictionConfig.VisualSmoothingRate <= 0.0f)
        {
            m_visualCorrectionOffset = Math::Vec3::Zero();
        }

        if (m_localOwnedNetId.IsValid() && m_predictionConfig.EnablePrediction)
        {
            const Scene::EntityId localEid = ResolveSceneEntity(m_localOwnedNetId);
            if (auto* tf = scene.Transforms().TryGet(localEid))
            {
                tf->Position = m_logicalPredictedPos + m_visualCorrectionOffset;
            }
        }

        if (m_predictionConfig.EnableRemoteInterpolation)
        {
            for (const auto& [packedNetId, eid] : m_netToScene)
            {
                const Network::NetworkId netId = Network::NetworkId::Unpack(packedNetId);
                if (netId == m_localOwnedNetId || !scene.IsAlive(eid))
                {
                    continue;
                }
                Network::ReplicatedTransformData interpTf{};
                if (m_remoteInterp.SampleEntity(netId, m_clientClockSec, interpTf))
                {
                    if (auto* tf = scene.Transforms().TryGet(eid))
                    {
                        (void)ReplicatedToSceneTransform(interpTf, *tf);
                    }
                }
            }
        }
    }

    Scene::EntityId SceneReplicationBridge::ResolveSceneEntity(Network::NetworkId netId) const noexcept
    {
        auto it = m_netToScene.find(netId.Pack());
        return it != m_netToScene.end() ? it->second : Scene::EntityId::Invalid();
    }

    Network::NetworkId SceneReplicationBridge::ResolveNetworkId(Scene::EntityId eid) const noexcept
    {
        auto it = m_sceneToNet.find(eid.Pack());
        return it != m_sceneToNet.end() ? it->second : Network::kInvalidNetworkId;
    }

    Network::NetworkId SceneReplicationBridge::ResolveStaticNetworkId(u64 staticNetId) const noexcept
    {
        auto it = m_staticToNet.find(staticNetId);
        return it != m_staticToNet.end() ? it->second : Network::kInvalidNetworkId;
    }

    bool SceneReplicationBridge::SendManagedRpc(
        Network::WorldInstanceId worldId,
        Network::NetworkId targetNetId,
        u32 behaviourHash,
        u32 methodHash,
        bool isServerRpc,
        u8 flags,
        std::span<const u8> argsBytes,
        Network::ConnectionManager& net,
        Network::ConnectionHandle clientTargetServerConn)
    {
        if (!m_initialized || !targetNetId.IsValid() || worldId == Network::kInvalidWorldInstanceId)
        {
            return false;
        }

        Network::ManagedRpcPacket pkt{};
        pkt.WireVersion = Network::ReplicationConstants::kManagedWireVersion;
        pkt.WorldId = worldId;
        pkt.TargetNetId = targetNetId;
        pkt.BehaviourTypeHash = behaviourHash;
        pkt.MethodHash = methodHash;
        pkt.RequireOwnership = (flags & 0x01u) != 0;
        pkt.TargetOwnerOnly = (flags & 0x02u) != 0;
        if (!DecodeManagedRpcArgsBytes(argsBytes, pkt.Args))
        {
            return false;
        }

        if (isServerRpc)
        {
            if (m_isServer)
            {
                return false;
            }
            const Network::WorldInstanceId activeW =
                (m_activeClientWorldId != Network::kInvalidWorldInstanceId) ? m_activeClientWorldId : m_config.WorldId;
            if (worldId != activeW)
            {
                return false;
            }
            Network::ClientReplicatedEntity clientEnt{};
            if (!m_clientApplier.TryGetEntity(targetNetId, clientEnt))
            {
                return false;
            }
            Network::ConnectionHandle targetConn = clientTargetServerConn;
            if (!targetConn.IsValid())
            {
                const auto conns = net.GetConnections();
                if (conns.empty())
                {
                    return false;
                }
                targetConn = conns.front();
            }

            Network::BitWriter payloadWriter(128);
            if (!Network::ReplicationCodec::WriteManagedRpc(payloadWriter, pkt))
            {
                return false;
            }
            Network::RpcInvocationEnvelope env{};
            env.RpcId = BuiltinRpcIds::kManagedServerRpc;
            env.TargetEntity = targetNetId;
            env.TargetWorldId = worldId;
            const auto payloadSpan = payloadWriter.GetData();
            env.Payload.assign(payloadSpan.begin(), payloadSpan.end());

            Network::BitWriter envWriter(160);
            if (!Network::RpcRouter::WriteInvocation(envWriter, env))
            {
                return false;
            }
            return net.SendPacket(
                targetConn,
                Network::MessageCategory::Rpc,
                0,
                envWriter.GetData(),
                Network::NetDelivery::ReliableOrdered,
                Network::NetChannel::GameplayEvents).Succeeded();
        }

        // Server -> Client RPC
        if (!m_isServer || worldId != m_config.WorldId)
        {
            return false;
        }
        Network::ServerReplicatedEntity ent{};
        if (!m_serverRep.TryGetEntity(targetNetId, ent))
        {
            return false;
        }

        Network::BitWriter payloadWriter(128);
        if (!Network::ReplicationCodec::WriteManagedRpc(payloadWriter, pkt))
        {
            return false;
        }
        Network::RpcInvocationEnvelope env{};
        env.RpcId = BuiltinRpcIds::kManagedClientRpc;
        env.TargetEntity = targetNetId;
        env.TargetWorldId = worldId;
        const auto payloadSpan = payloadWriter.GetData();
        env.Payload.assign(payloadSpan.begin(), payloadSpan.end());

        Network::BitWriter envWriter(160);
        if (!Network::RpcRouter::WriteInvocation(envWriter, env))
        {
            return false;
        }

        bool anySent = false;
        const auto conns = net.GetConnections();
        for (const auto conn : conns)
        {
            Network::ConnectionInfo ci{};
            if (!net.TryGetConnectionInfo(conn, ci) || !ci.Session.IsValid())
            {
                continue;
            }
            Network::NetworkSession ns{};
            if (!net.GetSessionRegistry().TryGetSession(ci.Session, ns) || ns.WorldId != m_config.WorldId)
            {
                continue;
            }
            if (pkt.TargetOwnerOnly && ci.Session != ent.OwnerSession)
            {
                continue;
            }
            if (net.SendPacket(
                    conn,
                    Network::MessageCategory::Rpc,
                    0,
                    envWriter.GetData(),
                    Network::NetDelivery::ReliableOrdered,
                    Network::NetChannel::GameplayEvents).Succeeded())
            {
                anySent = true;
            }
        }
        return anySent || !pkt.TargetOwnerOnly;
    }

    bool SceneReplicationBridge::SyncManagedProperties(
        Network::WorldInstanceId worldId,
        Network::NetworkId targetNetId,
        u32 behaviourHash,
        std::span<const u8> propsBytes,
        Network::ConnectionManager& net)
    {
        if (!m_initialized || !m_isServer || worldId != m_config.WorldId || !targetNetId.IsValid())
        {
            return false;
        }
        Network::ServerReplicatedEntity ent{};
        if (!m_serverRep.TryGetEntity(targetNetId, ent))
        {
            return false;
        }

        Network::ManagedPropertySyncPacket pkt{};
        pkt.WireVersion = Network::ReplicationConstants::kManagedWireVersion;
        pkt.WorldId = worldId;
        pkt.TargetNetId = targetNetId;
        pkt.BehaviourTypeHash = behaviourHash;
        if (!DecodeManagedPropsBytes(propsBytes, pkt.Properties) || pkt.Properties.empty())
        {
            return false;
        }

        // Merge into authoritative server cache so late-joining clients receive full state
        auto& entityCache = m_serverManagedPropCache[targetNetId.Pack()];
        std::vector<Network::ManagedPropertyEntry> merged;
        if (auto it = entityCache.find(behaviourHash); it != entityCache.end())
        {
            (void)DecodeManagedPropsBytes(it->second, merged);
        }
        for (const auto& incoming : pkt.Properties)
        {
            bool found = false;
            for (auto& existing : merged)
            {
                if (existing.PropertyId == incoming.PropertyId)
                {
                    existing.Value = incoming.Value;
                    found = true;
                    break;
                }
            }
            if (!found)
            {
                merged.push_back(incoming);
            }
        }
        std::vector<u8> encodedMerged;
        if (EncodeManagedPropsBytes(merged, encodedMerged))
        {
            entityCache[behaviourHash] = std::move(encodedMerged);
        }

        Network::BitWriter w(256);
        if (!Network::ReplicationCodec::WriteManagedPropertySync(w, pkt))
        {
            return false;
        }

        const auto conns = net.GetConnections();
        for (const auto conn : conns)
        {
            Network::ConnectionInfo ci{};
            if (!net.TryGetConnectionInfo(conn, ci) || !ci.Session.IsValid())
            {
                continue;
            }
            Network::NetworkSession ns{};
            if (!net.GetSessionRegistry().TryGetSession(ci.Session, ns) || ns.WorldId != m_config.WorldId)
            {
                continue;
            }
            (void)net.SendPacket(
                conn,
                Network::MessageCategory::Replication,
                static_cast<u8>(Network::ReplicationMessageSubType::ManagedPropertySync),
                w.GetData(),
                Network::NetDelivery::ReliableOrdered,
                Network::NetChannel::Snapshot);
        }
        return true;
    }
}
