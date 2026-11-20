#nullable enable
using System;
using System.Buffers.Binary;
using System.Collections.Generic;
using System.Reflection;
using System.Text;

namespace Morbi.Engine;

[AttributeUsage(AttributeTargets.Property, Inherited = true, AllowMultiple = false)]
public sealed class ReplicatedAttribute : Attribute
{
}

[AttributeUsage(AttributeTargets.Method, Inherited = true, AllowMultiple = false)]
public sealed class ServerRpcAttribute : Attribute
{
    public bool RequireOwnership { get; set; } = true;
}

[AttributeUsage(AttributeTargets.Method, Inherited = true, AllowMultiple = false)]
public sealed class ClientRpcAttribute : Attribute
{
    public bool OwnerOnly { get; set; } = false;
}

public readonly record struct NetworkId(uint Index, uint Generation)
{
    public static NetworkId Invalid => new(0u, 0u);
    public bool IsValid => Index != 0u && Generation != 0u;
    public ulong Pack() => ((ulong)Generation << 32) | Index;
    public static NetworkId Unpack(ulong packed) => new((uint)(packed & 0xFFFFFFFFu), (uint)(packed >> 32));
}

public readonly record struct WorldInstanceId(uint Value)
{
    public static WorldInstanceId Invalid => new(0u);
    public bool IsValid => Value != 0u;
}

public enum ManagedValueType : byte
{
    Int32 = 1,
    Float32 = 2,
    Boolean = 3,
    Vec3 = 4,
    String = 5,
    UInt64 = 6
}

internal readonly struct ManagedValue
{
    public readonly ManagedValueType Type;
    public readonly int IntVal;
    public readonly float FloatVal;
    public readonly Vec3 VecVal;
    public readonly ulong UInt64Val;
    public readonly bool BoolVal;
    public readonly string? StringVal;

    private ManagedValue(
        ManagedValueType type,
        int iVal = 0,
        float fVal = 0f,
        Vec3 vVal = default,
        ulong u64Val = 0ul,
        bool bVal = false,
        string? sVal = null)
    {
        Type = type;
        IntVal = iVal;
        FloatVal = fVal;
        VecVal = vVal;
        UInt64Val = u64Val;
        BoolVal = bVal;
        StringVal = sVal;
    }

    public static ManagedValue FromObject(object? value, Type expectedType)
    {
        if (expectedType == typeof(int))
        {
            return new ManagedValue(ManagedValueType.Int32, iVal: value is int iv ? iv : 0);
        }
        if (expectedType == typeof(float))
        {
            float fv = value is float f ? f : 0f;
            if (!float.IsFinite(fv)) throw new ArgumentException("Non-finite float is not valid for network replication.");
            return new ManagedValue(ManagedValueType.Float32, fVal: fv);
        }
        if (expectedType == typeof(bool))
        {
            return new ManagedValue(ManagedValueType.Boolean, bVal: value is bool bv && bv);
        }
        if (expectedType == typeof(Vec3))
        {
            Vec3 vv = value is Vec3 v ? v : Vec3.Zero;
            if (!float.IsFinite(vv.X) || !float.IsFinite(vv.Y) || !float.IsFinite(vv.Z) ||
                MathF.Abs(vv.X) > 100000f || MathF.Abs(vv.Y) > 100000f || MathF.Abs(vv.Z) > 100000f)
            {
                throw new ArgumentException("Vec3 value is non-finite or exceeds world coordinate bounds.");
            }
            return new ManagedValue(ManagedValueType.Vec3, vVal: vv);
        }
        if (expectedType == typeof(string))
        {
            string sv = value as string ?? string.Empty;
            if (Encoding.UTF8.GetByteCount(sv) > ManagedValueCodec.MaxStringBytes)
            {
                throw new ArgumentException($"String exceeds max network length ({ManagedValueCodec.MaxStringBytes} bytes).");
            }
            return new ManagedValue(ManagedValueType.String, sVal: sv);
        }
        if (expectedType == typeof(ulong))
        {
            return new ManagedValue(ManagedValueType.UInt64, u64Val: value is ulong uv ? uv : 0ul);
        }
        throw new NotSupportedException($"Type '{expectedType.FullName}' is not supported by MorbiEngine C# networking.");
    }

    public object ToObject() => Type switch
    {
        ManagedValueType.Int32 => IntVal,
        ManagedValueType.Float32 => FloatVal,
        ManagedValueType.Boolean => BoolVal,
        ManagedValueType.Vec3 => VecVal,
        ManagedValueType.String => StringVal ?? string.Empty,
        ManagedValueType.UInt64 => UInt64Val,
        _ => throw new NotSupportedException($"Unsupported ManagedValueType: {Type}")
    };

    public bool ValueEquals(in ManagedValue other)
    {
        if (Type != other.Type) return false;
        return Type switch
        {
            ManagedValueType.Int32 => IntVal == other.IntVal,
            ManagedValueType.Float32 => MathF.Abs(FloatVal - other.FloatVal) <= 1e-5f,
            ManagedValueType.Boolean => BoolVal == other.BoolVal,
            ManagedValueType.Vec3 => MathF.Abs(VecVal.X - other.VecVal.X) <= 1e-4f &&
                                     MathF.Abs(VecVal.Y - other.VecVal.Y) <= 1e-4f &&
                                     MathF.Abs(VecVal.Z - other.VecVal.Z) <= 1e-4f,
            ManagedValueType.String => string.Equals(StringVal ?? string.Empty, other.StringVal ?? string.Empty, StringComparison.Ordinal),
            ManagedValueType.UInt64 => UInt64Val == other.UInt64Val,
            _ => false
        };
    }
}

internal static class ManagedValueCodec
{
    public const int MaxPropertiesPerSync = 32;
    public const int MaxRpcArgs = 8;
    public const int MaxStringBytes = 128;

    public static bool IsSupportedType(Type t)
    {
        return t == typeof(int) ||
               t == typeof(float) ||
               t == typeof(bool) ||
               t == typeof(Vec3) ||
               t == typeof(string) ||
               t == typeof(ulong);
    }

    public static void WriteValue(List<byte> buffer, in ManagedValue val)
    {
        buffer.Add((byte)val.Type);
        Span<byte> tmp = stackalloc byte[12];
        switch (val.Type)
        {
            case ManagedValueType.Int32:
                BinaryPrimitives.WriteInt32LittleEndian(tmp, val.IntVal);
                for (int i = 0; i < 4; i++) buffer.Add(tmp[i]);
                break;
            case ManagedValueType.Float32:
                BinaryPrimitives.WriteSingleLittleEndian(tmp, val.FloatVal);
                for (int i = 0; i < 4; i++) buffer.Add(tmp[i]);
                break;
            case ManagedValueType.Boolean:
                buffer.Add(val.BoolVal ? (byte)1 : (byte)0);
                break;
            case ManagedValueType.Vec3:
                BinaryPrimitives.WriteSingleLittleEndian(tmp[0..4], val.VecVal.X);
                BinaryPrimitives.WriteSingleLittleEndian(tmp[4..8], val.VecVal.Y);
                BinaryPrimitives.WriteSingleLittleEndian(tmp[8..12], val.VecVal.Z);
                for (int i = 0; i < 12; i++) buffer.Add(tmp[i]);
                break;
            case ManagedValueType.String:
            {
                byte[] utf8 = Encoding.UTF8.GetBytes(val.StringVal ?? string.Empty);
                if (utf8.Length > MaxStringBytes)
                {
                    throw new ArgumentException("String exceeds MaxStringBytes.");
                }
                BinaryPrimitives.WriteUInt16LittleEndian(tmp, (ushort)utf8.Length);
                buffer.Add(tmp[0]);
                buffer.Add(tmp[1]);
                for (int i = 0; i < utf8.Length; i++) buffer.Add(utf8[i]);
                break;
            }
            case ManagedValueType.UInt64:
                BinaryPrimitives.WriteUInt64LittleEndian(tmp, val.UInt64Val);
                for (int i = 0; i < 8; i++) buffer.Add(tmp[i]);
                break;
            default:
                throw new NotSupportedException($"Unsupported ManagedValueType: {val.Type}");
        }
    }

    public static bool TryReadValue(ReadOnlySpan<byte> data, ref int offset, out ManagedValue value)
    {
        value = default;
        if (offset >= data.Length) return false;
        byte typeRaw = data[offset++];
        if (typeRaw < (byte)ManagedValueType.Int32 || typeRaw > (byte)ManagedValueType.UInt64) return false;
        var type = (ManagedValueType)typeRaw;

        switch (type)
        {
            case ManagedValueType.Int32:
                if (offset + 4 > data.Length) return false;
                int iv = BinaryPrimitives.ReadInt32LittleEndian(data.Slice(offset, 4));
                offset += 4;
                value = ManagedValue.FromObject(iv, typeof(int));
                return true;
            case ManagedValueType.Float32:
            {
                if (offset + 4 > data.Length) return false;
                float fv = BinaryPrimitives.ReadSingleLittleEndian(data.Slice(offset, 4));
                offset += 4;
                if (!float.IsFinite(fv)) return false;
                value = ManagedValue.FromObject(fv, typeof(float));
                return true;
            }
            case ManagedValueType.Boolean:
                if (offset >= data.Length) return false;
                byte b = data[offset++];
                if (b > 1) return false;
                value = ManagedValue.FromObject(b != 0, typeof(bool));
                return true;
            case ManagedValueType.Vec3:
            {
                if (offset + 12 > data.Length) return false;
                float x = BinaryPrimitives.ReadSingleLittleEndian(data.Slice(offset, 4));
                float y = BinaryPrimitives.ReadSingleLittleEndian(data.Slice(offset + 4, 4));
                float z = BinaryPrimitives.ReadSingleLittleEndian(data.Slice(offset + 8, 4));
                offset += 12;
                if (!float.IsFinite(x) || !float.IsFinite(y) || !float.IsFinite(z) ||
                    MathF.Abs(x) > 100000f || MathF.Abs(y) > 100000f || MathF.Abs(z) > 100000f)
                {
                    return false;
                }
                value = ManagedValue.FromObject(new Vec3(x, y, z), typeof(Vec3));
                return true;
            }
            case ManagedValueType.String:
            {
                if (offset + 2 > data.Length) return false;
                ushort len = BinaryPrimitives.ReadUInt16LittleEndian(data.Slice(offset, 2));
                offset += 2;
                if (len > MaxStringBytes || offset + len > data.Length) return false;
                string sv = Encoding.UTF8.GetString(data.Slice(offset, len));
                offset += len;
                value = ManagedValue.FromObject(sv, typeof(string));
                return true;
            }
            case ManagedValueType.UInt64:
                if (offset + 8 > data.Length) return false;
                ulong uv = BinaryPrimitives.ReadUInt64LittleEndian(data.Slice(offset, 8));
                offset += 8;
                value = ManagedValue.FromObject(uv, typeof(ulong));
                return true;
            default:
                return false;
        }
    }

    public static byte[] EncodeRpcArgs(ReadOnlySpan<ManagedValue> args)
    {
        if (args.Length > MaxRpcArgs)
        {
            throw new ArgumentException($"RPC argument count ({args.Length}) exceeds MaxRpcArgs ({MaxRpcArgs}).");
        }
        var buf = new List<byte>(1 + args.Length * 8) { (byte)args.Length };
        for (int i = 0; i < args.Length; i++)
        {
            WriteValue(buf, args[i]);
        }
        return buf.ToArray();
    }

    public static bool TryDecodeRpcArgs(ReadOnlySpan<byte> data, out ManagedValue[] args)
    {
        args = Array.Empty<ManagedValue>();
        if (data.IsEmpty) return true;
        int offset = 0;
        byte count = data[offset++];
        if (count > MaxRpcArgs) return false;
        if (count == 0) return offset == data.Length;

        var list = new ManagedValue[count];
        for (int i = 0; i < count; i++)
        {
            if (!TryReadValue(data, ref offset, out list[i])) return false;
        }
        if (offset != data.Length) return false;
        args = list;
        return true;
    }

    public static byte[] EncodeProperties(IReadOnlyList<(uint PropertyId, ManagedValue Value)> props)
    {
        if (props.Count > MaxPropertiesPerSync)
        {
            throw new InvalidOperationException($"Exceeded MaxPropertiesPerSync ({MaxPropertiesPerSync}).");
        }
        var buf = new List<byte>(2 + props.Count * 12);
        Span<byte> hdr = stackalloc byte[4];
        BinaryPrimitives.WriteUInt16LittleEndian(hdr, (ushort)props.Count);
        buf.Add(hdr[0]);
        buf.Add(hdr[1]);
        for (int i = 0; i < props.Count; i++)
        {
            BinaryPrimitives.WriteUInt32LittleEndian(hdr, props[i].PropertyId);
            buf.Add(hdr[0]);
            buf.Add(hdr[1]);
            buf.Add(hdr[2]);
            buf.Add(hdr[3]);
            WriteValue(buf, props[i].Value);
        }
        return buf.ToArray();
    }

    public static bool TryDecodeProperties(ReadOnlySpan<byte> data, List<(uint PropertyId, ManagedValue Value)> outProps)
    {
        outProps.Clear();
        if (data.Length < 2) return false;
        int offset = 0;
        ushort count = BinaryPrimitives.ReadUInt16LittleEndian(data.Slice(offset, 2));
        offset += 2;
        if (count == 0 || count > MaxPropertiesPerSync) return false;

        outProps.Capacity = Math.Max(outProps.Capacity, count);
        for (int i = 0; i < count; i++)
        {
            if (offset + 4 > data.Length) return false;
            uint propId = BinaryPrimitives.ReadUInt32LittleEndian(data.Slice(offset, 4));
            offset += 4;
            if (propId == 0 || !TryReadValue(data, ref offset, out ManagedValue val)) return false;
            outProps.Add((propId, val));
        }
        return offset == data.Length;
    }
}

public sealed class NetworkObject
{
    private readonly List<NetworkBehaviour> _behaviours = new();
    private bool _disposed;

    internal NetworkObject(
        WorldInstanceId worldId,
        NetworkId networkId,
        EntityId entityId,
        ulong ownerPlayerId,
        bool isServer,
        bool isClient,
        bool isOwner)
    {
        WorldId = worldId;
        NetworkId = networkId;
        EntityId = entityId;
        OwnerPlayerId = ownerPlayerId;
        IsServer = isServer;
        IsClient = isClient;
        IsOwner = isOwner;
        IsSpawned = true;
    }

    public WorldInstanceId WorldId { get; internal set; }
    public NetworkId NetworkId { get; }
    public EntityId EntityId { get; internal set; }
    public ulong OwnerPlayerId { get; internal set; }
    public bool IsServer { get; }
    public bool IsClient { get; }
    public bool IsOwner { get; internal set; }
    public bool IsSpawned { get; internal set; }

    public bool IsValid
    {
        get
        {
            if (_disposed || !IsSpawned || !WorldId.IsValid || !NetworkId.IsValid) return false;
            return NativeBridge.NetGetObjectState(WorldId.Value, NetworkId.Pack(), out _, out _, out _, out _, out _);
        }
    }

    internal void EnsureValid()
    {
        if (!IsValid)
        {
            throw new ObjectDisposedException(nameof(NetworkObject),
                $"NetworkObject ({NetworkId.Index}:{NetworkId.Generation}) in World {WorldId.Value} is despawned or invalid.");
        }
    }

    internal void RegisterBehaviour(NetworkBehaviour behaviour)
    {
        if (!_behaviours.Contains(behaviour))
        {
            _behaviours.Add(behaviour);
        }
    }

    public T? GetBehaviour<T>() where T : NetworkBehaviour
    {
        EnsureValid();
        for (int i = 0; i < _behaviours.Count; i++)
        {
            if (_behaviours[i] is T match) return match;
        }
        return null;
    }

    internal IReadOnlyList<NetworkBehaviour> Behaviours => _behaviours;

    internal void MarkDespawned()
    {
        IsSpawned = false;
        _disposed = true;
    }
}

public abstract class NetworkBehaviour : Script
{
    internal sealed class ReplicatedPropSlot
    {
        public uint PropertyId;
        public PropertyInfo Property = null!;
        public Type PropertyType = null!;
    }

    internal sealed class RpcMethodSlot
    {
        public uint MethodHash;
        public MethodInfo Method = null!;
        public ParameterInfo[] Parameters = Array.Empty<ParameterInfo>();
        public bool IsServerRpc;
        public bool RequireOwnership;
        public bool OwnerOnly;
    }

    internal sealed class BehaviourMetadata
    {
        public uint TypeHash;
        public List<ReplicatedPropSlot> ReplicatedProps = new();
        public Dictionary<uint, RpcMethodSlot> ServerRpcsByHash = new();
        public Dictionary<string, RpcMethodSlot> ServerRpcsByName = new(StringComparer.Ordinal);
        public Dictionary<uint, RpcMethodSlot> ClientRpcsByHash = new();
        public Dictionary<string, RpcMethodSlot> ClientRpcsByName = new(StringComparer.Ordinal);
    }

    private static readonly Dictionary<Type, BehaviourMetadata> s_metadataCache = new();

    private NetworkObject? _networkObject;
    private readonly BehaviourMetadata _metadata;
    private readonly Dictionary<uint, ManagedValue> _lastSyncedProps = new();

    protected NetworkBehaviour()
    {
        _metadata = GetOrBuildMetadata(GetType());
    }

    public NetworkObject NetworkObject => _networkObject
        ?? throw new InvalidOperationException($"{GetType().Name} is not bound to a NetworkObject.");

    public WorldInstanceId WorldId => _networkObject?.WorldId ?? WorldInstanceId.Invalid;
    public bool IsServer => _networkObject?.IsServer ?? false;
    public bool IsClient => _networkObject?.IsClient ?? false;
    public bool IsOwner => _networkObject?.IsOwner ?? false;
    public bool IsSpawned => _networkObject?.IsSpawned ?? false;

    internal uint BehaviourTypeHash => _metadata.TypeHash;

    internal void AttachNetworkObject(NetworkObject netObj)
    {
        _networkObject = netObj;
        netObj.RegisterBehaviour(this);
        _lastSyncedProps.Clear();
    }

    internal void DetachNetworkObject()
    {
        _networkObject = null;
        _lastSyncedProps.Clear();
    }

    public virtual void OnNetworkSpawn() { }
    public virtual void OnNetworkDespawn() { }
    public virtual void OnNetworkOwnershipChanged(ulong previousOwnerPlayerId, ulong newOwnerPlayerId) { }
    public virtual void OnNetworkWorldChanged(WorldInstanceId previousWorld, WorldInstanceId newWorld) { }

    protected bool InvokeServerRpc(string methodName, params object[] args)
    {
        if (_networkObject == null || !_networkObject.IsValid)
        {
            throw new ObjectDisposedException(nameof(NetworkBehaviour), "Cannot invoke ServerRpc on despawned/disposed NetworkBehaviour.");
        }
        if (!_networkObject.IsClient)
        {
            return false;
        }
        if (!_metadata.ServerRpcsByName.TryGetValue(methodName, out RpcMethodSlot? slot))
        {
            throw new MissingMethodException(GetType().FullName, methodName);
        }
        if (slot.RequireOwnership && !_networkObject.IsOwner)
        {
            return false;
        }
        return SendRpcInternal(slot, args);
    }

    protected bool InvokeClientRpc(string methodName, params object[] args)
    {
        if (_networkObject == null || !_networkObject.IsValid)
        {
            throw new ObjectDisposedException(nameof(NetworkBehaviour), "Cannot invoke ClientRpc on despawned/disposed NetworkBehaviour.");
        }
        if (!_networkObject.IsServer)
        {
            return false;
        }
        if (!_metadata.ClientRpcsByName.TryGetValue(methodName, out RpcMethodSlot? slot))
        {
            throw new MissingMethodException(GetType().FullName, methodName);
        }
        return SendRpcInternal(slot, args);
    }

    private bool SendRpcInternal(RpcMethodSlot slot, object[] args)
    {
        if (_networkObject == null) return false;
        if (args.Length != slot.Parameters.Length)
        {
            throw new ArgumentException($"RPC '{slot.Method.Name}' expects {slot.Parameters.Length} argument(s), got {args.Length}.");
        }
        var encodedArgs = new ManagedValue[args.Length];
        for (int i = 0; i < args.Length; i++)
        {
            encodedArgs[i] = ManagedValue.FromObject(args[i], slot.Parameters[i].ParameterType);
        }
        byte[] bytes = ManagedValueCodec.EncodeRpcArgs(encodedArgs);
        byte flags = (byte)((slot.RequireOwnership ? 1 : 0) | (slot.OwnerOnly ? 2 : 0));
        return NativeBridge.NetSendManagedRpc(
            _networkObject.WorldId.Value,
            _networkObject.NetworkId.Pack(),
            _metadata.TypeHash,
            slot.MethodHash,
            slot.IsServerRpc,
            flags,
            bytes);
    }

    internal void SyncDirtyPropertiesToNative()
    {
        if (_networkObject == null || !_networkObject.IsServer || !_networkObject.IsSpawned || _metadata.ReplicatedProps.Count == 0)
        {
            return;
        }

        var dirty = new List<(uint PropertyId, ManagedValue Value)>();
        for (int i = 0; i < _metadata.ReplicatedProps.Count; i++)
        {
            ReplicatedPropSlot slot = _metadata.ReplicatedProps[i];
            object? raw = slot.Property.GetValue(this);
            ManagedValue current = ManagedValue.FromObject(raw, slot.PropertyType);
            if (!_lastSyncedProps.TryGetValue(slot.PropertyId, out ManagedValue prev) || !current.ValueEquals(prev))
            {
                dirty.Add((slot.PropertyId, current));
            }
        }

        if (dirty.Count == 0) return;

        int offset = 0;
        while (offset < dirty.Count)
        {
            int batchCount = Math.Min(ManagedValueCodec.MaxPropertiesPerSync, dirty.Count - offset);
            var batch = dirty.GetRange(offset, batchCount);
            byte[] bytes = ManagedValueCodec.EncodeProperties(batch);
            if (NativeBridge.NetSyncManagedProperties(
                    _networkObject.WorldId.Value,
                    _networkObject.NetworkId.Pack(),
                    _metadata.TypeHash,
                    bytes))
            {
                for (int i = 0; i < batch.Count; i++)
                {
                    _lastSyncedProps[batch[i].PropertyId] = batch[i].Value;
                }
            }
            offset += batchCount;
        }
    }

    internal bool ApplyAuthoritativeProperties(ReadOnlySpan<byte> propsBytes)
    {
        if (_networkObject == null || _networkObject.IsServer)
        {
            // Clients apply authoritative server updates; servers never accept client property overwrites
            return false;
        }
        var decoded = new List<(uint PropertyId, ManagedValue Value)>();
        if (!ManagedValueCodec.TryDecodeProperties(propsBytes, decoded))
        {
            return false;
        }

        for (int i = 0; i < decoded.Count; i++)
        {
            (uint propId, ManagedValue val) = decoded[i];
            for (int j = 0; j < _metadata.ReplicatedProps.Count; j++)
            {
                ReplicatedPropSlot slot = _metadata.ReplicatedProps[j];
                if (slot.PropertyId == propId && slot.Property.CanWrite)
                {
                    slot.Property.SetValue(this, val.ToObject());
                    _lastSyncedProps[propId] = val;
                    break;
                }
            }
        }
        return true;
    }

    internal bool DispatchRpcMethod(uint methodHash, bool isServerSide, ReadOnlySpan<byte> argsBytes)
    {
        if (_networkObject == null || !_networkObject.IsValid) return false;

        RpcMethodSlot? slot = null;
        if (isServerSide)
        {
            _metadata.ServerRpcsByHash.TryGetValue(methodHash, out slot);
        }
        else
        {
            _metadata.ClientRpcsByHash.TryGetValue(methodHash, out slot);
        }
        if (slot == null) return false;

        if (!ManagedValueCodec.TryDecodeRpcArgs(argsBytes, out ManagedValue[] decodedArgs))
        {
            return false;
        }
        if (decodedArgs.Length != slot.Parameters.Length)
        {
            return false;
        }

        var invokeArgs = new object?[decodedArgs.Length];
        for (int i = 0; i < decodedArgs.Length; i++)
        {
            invokeArgs[i] = decodedArgs[i].ToObject();
        }
        slot.Method.Invoke(this, invokeArgs);
        return true;
    }

    internal static uint Hash32(string value)
    {
        ulong h64 = ManagedHost.ComputeStringId(value);
        uint h32 = (uint)(h64 ^ (h64 >> 32));
        return h32 == 0u ? 1u : h32;
    }

    private static BehaviourMetadata GetOrBuildMetadata(Type type)
    {
        if (s_metadataCache.TryGetValue(type, out BehaviourMetadata? existing))
        {
            return existing;
        }

        var meta = new BehaviourMetadata
        {
            TypeHash = Hash32(type.FullName ?? type.Name)
        };

        const BindingFlags flags = BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic;
        foreach (PropertyInfo prop in type.GetProperties(flags))
        {
            if (prop.GetCustomAttribute<ReplicatedAttribute>() == null) continue;
            if (!ManagedValueCodec.IsSupportedType(prop.PropertyType))
            {
                throw new NotSupportedException(
                    $"[Replicated] property '{type.Name}.{prop.Name}' has unsupported type '{prop.PropertyType.FullName}'.");
            }
            if (!prop.CanRead || !prop.CanWrite)
            {
                throw new NotSupportedException(
                    $"[Replicated] property '{type.Name}.{prop.Name}' must have both getter and setter.");
            }
            meta.ReplicatedProps.Add(new ReplicatedPropSlot
            {
                PropertyId = Hash32(prop.Name),
                Property = prop,
                PropertyType = prop.PropertyType
            });
        }

        foreach (MethodInfo method in type.GetMethods(flags))
        {
            var srvAttr = method.GetCustomAttribute<ServerRpcAttribute>();
            var cliAttr = method.GetCustomAttribute<ClientRpcAttribute>();
            if (srvAttr == null && cliAttr == null) continue;

            ParameterInfo[] parameters = method.GetParameters();
            if (parameters.Length > ManagedValueCodec.MaxRpcArgs)
            {
                throw new NotSupportedException(
                    $"RPC method '{type.Name}.{method.Name}' exceeds max parameter count ({ManagedValueCodec.MaxRpcArgs}).");
            }
            foreach (ParameterInfo p in parameters)
            {
                if (!ManagedValueCodec.IsSupportedType(p.ParameterType))
                {
                    throw new NotSupportedException(
                        $"RPC method '{type.Name}.{method.Name}' parameter '{p.Name}' has unsupported type '{p.ParameterType.FullName}'.");
                }
            }

            uint mHash = Hash32(method.Name);
            if (srvAttr != null)
            {
                var slot = new RpcMethodSlot
                {
                    MethodHash = mHash,
                    Method = method,
                    Parameters = parameters,
                    IsServerRpc = true,
                    RequireOwnership = srvAttr.RequireOwnership
                };
                meta.ServerRpcsByHash[mHash] = slot;
                meta.ServerRpcsByName[method.Name] = slot;
            }
            if (cliAttr != null)
            {
                var slot = new RpcMethodSlot
                {
                    MethodHash = mHash,
                    Method = method,
                    Parameters = parameters,
                    IsServerRpc = false,
                    OwnerOnly = cliAttr.OwnerOnly
                };
                meta.ClientRpcsByHash[mHash] = slot;
                meta.ClientRpcsByName[method.Name] = slot;
            }
        }

        s_metadataCache[type] = meta;
        return meta;
    }
}

public static class NetworkWorldRegistry
{
    private static readonly Dictionary<uint, Dictionary<ulong, NetworkObject>> s_worlds = new();

    public static NetworkObject? FindObject(WorldInstanceId worldId, NetworkId netId)
    {
        if (s_worlds.TryGetValue(worldId.Value, out var map) && map.TryGetValue(netId.Pack(), out var obj))
        {
            return obj;
        }
        return null;
    }

    public static int GetWorldObjectCount(WorldInstanceId worldId)
    {
        return s_worlds.TryGetValue(worldId.Value, out var map) ? map.Count : 0;
    }

    internal static void ClearAll()
    {
        foreach (var kv in s_worlds)
        {
            foreach (var netObj in kv.Value.Values)
            {
                DespawnObjectInternal(netObj);
            }
        }
        s_worlds.Clear();
    }

    internal static void OnWorldDestroyed(uint worldId)
    {
        if (!s_worlds.TryGetValue(worldId, out var map)) return;
        foreach (var netObj in map.Values)
        {
            DespawnObjectInternal(netObj);
        }
        s_worlds.Remove(worldId);
    }

    internal static void OnNetworkSpawn(
        uint worldId,
        ulong netIdPacked,
        ulong sceneEntityPacked,
        ulong ownerPlayerId,
        bool isServer,
        bool isClient,
        bool isOwner,
        IReadOnlyList<Script>? attachedScripts)
    {
        if (!s_worlds.TryGetValue(worldId, out var map))
        {
            map = new Dictionary<ulong, NetworkObject>();
            s_worlds[worldId] = map;
        }

        var wid = new WorldInstanceId(worldId);
        var nid = NetworkId.Unpack(netIdPacked);
        var eid = EntityId.Unpack(sceneEntityPacked);

        if (!map.TryGetValue(netIdPacked, out var netObj))
        {
            netObj = new NetworkObject(wid, nid, eid, ownerPlayerId, isServer, isClient, isOwner);
            map[netIdPacked] = netObj;
        }
        else
        {
            netObj.EntityId = eid;
            netObj.OwnerPlayerId = ownerPlayerId;
            netObj.IsOwner = isOwner;
            netObj.IsSpawned = true;
        }

        if (attachedScripts != null)
        {
            for (int i = 0; i < attachedScripts.Count; i++)
            {
                if (attachedScripts[i] is NetworkBehaviour nb)
                {
                    nb.AttachNetworkObject(netObj);
                    nb.OnNetworkSpawn();
                }
            }
        }
    }

    internal static void OnNetworkDespawn(uint worldId, ulong netIdPacked)
    {
        if (!s_worlds.TryGetValue(worldId, out var map)) return;
        if (!map.TryGetValue(netIdPacked, out var netObj)) return;
        DespawnObjectInternal(netObj);
        map.Remove(netIdPacked);
    }

    internal static void OnNetworkOwnershipChanged(
        uint worldId,
        ulong netIdPacked,
        ulong prevOwner,
        ulong newOwner,
        bool isOwner)
    {
        if (!s_worlds.TryGetValue(worldId, out var map)) return;
        if (!map.TryGetValue(netIdPacked, out var netObj)) return;
        netObj.OwnerPlayerId = newOwner;
        netObj.IsOwner = isOwner;
        foreach (var nb in netObj.Behaviours)
        {
            nb.OnNetworkOwnershipChanged(prevOwner, newOwner);
        }
    }

    internal static void OnNetworkWorldChanged(uint prevWorldId, uint newWorldId, ulong netIdPacked)
    {
        NetworkObject? obj = null;
        if (s_worlds.TryGetValue(prevWorldId, out var oldMap) && oldMap.TryGetValue(netIdPacked, out obj))
        {
            oldMap.Remove(netIdPacked);
        }
        if (obj == null) return;
        var prev = new WorldInstanceId(prevWorldId);
        var next = new WorldInstanceId(newWorldId);
        obj.WorldId = next;
        if (!s_worlds.TryGetValue(newWorldId, out var newMap))
        {
            newMap = new Dictionary<ulong, NetworkObject>();
            s_worlds[newWorldId] = newMap;
        }
        newMap[netIdPacked] = obj;
        foreach (var nb in obj.Behaviours)
        {
            nb.OnNetworkWorldChanged(prev, next);
        }
    }

    internal static bool DispatchPropertySync(uint worldId, ulong netIdPacked, uint behaviourHash, ReadOnlySpan<byte> propsBytes)
    {
        if (!s_worlds.TryGetValue(worldId, out var map)) return false;
        if (!map.TryGetValue(netIdPacked, out var netObj) || !netObj.IsValid) return false;
        foreach (var nb in netObj.Behaviours)
        {
            if (nb.BehaviourTypeHash == behaviourHash)
            {
                return nb.ApplyAuthoritativeProperties(propsBytes);
            }
        }
        return false;
    }

    internal static bool DispatchRpc(
        uint worldId,
        ulong netIdPacked,
        uint behaviourHash,
        uint methodHash,
        bool isServerSide,
        ReadOnlySpan<byte> argsBytes)
    {
        if (!s_worlds.TryGetValue(worldId, out var map)) return false;
        if (!map.TryGetValue(netIdPacked, out var netObj) || !netObj.IsValid) return false;
        foreach (var nb in netObj.Behaviours)
        {
            if (nb.BehaviourTypeHash == behaviourHash)
            {
                return nb.DispatchRpcMethod(methodHash, isServerSide, argsBytes);
            }
        }
        return false;
    }

    internal static void SyncDirtyPropertiesForWorld(uint worldId)
    {
        if (!s_worlds.TryGetValue(worldId, out var map)) return;
        foreach (var netObj in map.Values)
        {
            if (!netObj.IsServer || !netObj.IsSpawned) continue;
            foreach (var nb in netObj.Behaviours)
            {
                nb.SyncDirtyPropertiesToNative();
            }
        }
    }

    private static void DespawnObjectInternal(NetworkObject netObj)
    {
        if (!netObj.IsSpawned) return;
        foreach (var nb in netObj.Behaviours)
        {
            nb.OnNetworkDespawn();
            nb.DetachNetworkObject();
        }
        netObj.MarkDespawned();
    }
}
