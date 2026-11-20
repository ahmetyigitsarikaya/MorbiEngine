#nullable enable
using System;
using System.Collections.Generic;
using System.IO;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Runtime.Loader;
using System.Text;
using System.Threading.Tasks;

namespace Morbi.Engine;

internal sealed class CollectibleScriptLoadContext : AssemblyLoadContext
{
    public CollectibleScriptLoadContext(string name)
        : base(name, isCollectible: true)
    {
    }

    protected override Assembly? Load(AssemblyName assemblyName)
    {
        if (string.Equals(assemblyName.Name, typeof(ManagedHost).Assembly.GetName().Name, StringComparison.OrdinalIgnoreCase))
        {
            return typeof(ManagedHost).Assembly;
        }
        return null;
    }
}

public static unsafe class ManagedHost
{
    private static CollectibleScriptLoadContext? s_activeAlc;
    private static WeakReference? s_alcWeakRef;
    private static readonly List<IMorbiScript> s_activeScripts = new();
    private static readonly Dictionary<ulong, List<Script>> s_entityScripts = new();
    private static readonly Dictionary<ulong, Type> s_scriptTypesByHash = new();
    private static readonly List<Script> s_tickSnapshot = new();
    private static ulong s_nextInstanceToken = 1;
    private static uint s_reloadCount;
    private static bool s_exceptionHooksRegistered;

    private static void EnsureGlobalExceptionHooks()
    {
        if (s_exceptionHooksRegistered) return;
        s_exceptionHooksRegistered = true;

        AppDomain.CurrentDomain.UnhandledException += (_, e) =>
        {
            if (e.ExceptionObject is Exception ex)
            {
                NativeBridge.ReportFatalException(ex, $"AppDomain.UnhandledException (IsTerminating={e.IsTerminating})");
            }
            else
            {
                NativeBridge.LogInfo("C#.Exception", $"Non-CLS UnhandledException: {e.ExceptionObject}");
            }
        };

        TaskScheduler.UnobservedTaskException += (_, e) =>
        {
            NativeBridge.ReportFatalException(e.Exception, "TaskScheduler.UnobservedTaskException");
        };
    }

    internal static ulong ComputeStringId(string? value)
    {
        if (string.IsNullOrEmpty(value)) return 0ul;
        const ulong offsetBasis = 14695981039346656037ul;
        const ulong prime = 1099511628211ul;

        ulong hash = offsetBasis;
        byte[] utf8 = Encoding.UTF8.GetBytes(value);
        for (int i = 0; i < utf8.Length; i++)
        {
            hash ^= utf8[i];
            hash *= prime;
        }
        return hash;
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int Initialize(NativeEngineFunctionTable* table, int tableSize)
    {
        try
        {
            if (table == null || tableSize < sizeof(NativeEngineFunctionTable))
            {
                return -1;
            }
            if (sizeof(TransformComponent) != 40) return -2;
            if (sizeof(HierarchyComponent) != 32) return -3;
            if (sizeof(CameraComponent) != 32) return -4;
            if (sizeof(MeshRendererComponent) != 32) return -5;
            if (sizeof(LightComponent) != 32) return -6;
            if (sizeof(ScriptComponent) != 32) return -7;
            if (sizeof(RigidBodyComponent) != 116) return -8;
            if (sizeof(AudioSourceComponent) != 32) return -9;
            if (sizeof(NativeQueryFilter) != 16) return -13;
            if (sizeof(NativeRaycastHit) != 40) return -14;
            if (sizeof(NativeOverlapHit) != 40) return -15;
            if (sizeof(NativeCollisionEvent) != 64) return -16;
            if (sizeof(CharacterControllerComponent) != 112) return -17;
            if (sizeof(NativeCharacterState) != 56) return -18;

            NativeBridge.Bind(table);
            EnsureGlobalExceptionHooks();
            NativeBridge.LogInfo("C#.Host", "Morbi.Engine (.NET CoreCLR) initialized with Zero-Copy Component Spans (API v6), Jolt Physics Bridge & Roslyn ScriptCompiler.");
            return 0;
        }
        catch (Exception ex)
        {
            NativeBridge.ReportFatalException(ex, "ManagedHost.Initialize");
            return -99;
        }
    }

    internal static T? AttachScript<T>(Entity entity) where T : Script, new()
    {
        if (!entity.IsValid || !entity.IsAlive) return null;

        if (!NativeBridge.HasComponent(entity.Id, ComponentPoolKind.Script))
        {
            if (!NativeBridge.AddComponent(entity.Id, ComponentPoolKind.Script))
            {
                return null;
            }
        }

        ulong classHash = ComputeStringId(typeof(T).FullName ?? typeof(T).Name);
        if (!s_scriptTypesByHash.ContainsKey(classHash))
        {
            s_scriptTypesByHash[classHash] = typeof(T);
            ulong shortHash = ComputeStringId(typeof(T).Name);
            s_scriptTypesByHash.TryAdd(shortHash, typeof(T));
        }

        var instance = new T();
        instance.BindEntity(entity);

        ulong token = s_nextInstanceToken++;
        Span<ScriptComponent> scripts = NativeBridge.GetScriptSpan(out ReadOnlySpan<EntityId> entities);
        int idx = ComponentLookup.FindDenseIndex(entity.Id, entities);
        if (idx >= 0 && idx < scripts.Length)
        {
            scripts[idx].ScriptClassId = classHash;
            scripts[idx].ManagedGCHandle = token;
            scripts[idx].Enabled = 1u;
        }

        RegisterEntityScript(entity.Id.Pack(), instance);
        if (instance is NetworkBehaviour nb)
        {
            uint activeWid = NativeBridge.GetActiveWorldId();
            if (NativeBridge.NetResolveEntityNetworkId(activeWid, entity.Id, out ulong netIdPacked))
            {
                NetworkObject? netObj = NetworkWorldRegistry.FindObject(new WorldInstanceId(activeWid), NetworkId.Unpack(netIdPacked));
                if (netObj != null && netObj.IsSpawned)
                {
                    nb.AttachNetworkObject(netObj);
                    try { nb.OnNetworkSpawn(); }
                    catch (Exception ex) { NativeBridge.ReportFatalException(ex, $"AttachScript<{typeof(T).Name}>.OnNetworkSpawn"); }
                }
            }
        }
        try
        {
            instance.OnStart();
        }
        catch (Exception ex)
        {
            NativeBridge.ReportFatalException(ex, $"AttachScript<{typeof(T).Name}>.OnStart");
        }
        return instance;
    }

    internal static T? FindEntityScript<T>(EntityId entityId) where T : Script
    {
        if (!entityId.IsValid) return null;
        if (!s_entityScripts.TryGetValue(entityId.Pack(), out List<Script>? list)) return null;
        for (int i = 0; i < list.Count; i++)
        {
            if (list[i] is T typed) return typed;
        }
        return null;
    }

    private static void RegisterEntityScript(ulong packedEntityId, Script script)
    {
        if (!s_entityScripts.TryGetValue(packedEntityId, out List<Script>? list))
        {
            list = new List<Script>(1);
            s_entityScripts[packedEntityId] = list;
        }
        list.Add(script);
    }

    private static HashSet<Type> SyncEntityScriptsFromArena()
    {
        var boundTypes = new HashSet<Type>();
        if (s_scriptTypesByHash.Count == 0) return boundTypes;

        Span<ScriptComponent> scriptComponents = NativeBridge.GetScriptSpan(out ReadOnlySpan<EntityId> entities);
        for (int i = 0; i < scriptComponents.Length; i++)
        {
            ref ScriptComponent comp = ref scriptComponents[i];
            if (comp.Enabled == 0u || comp.ScriptClassId == 0ul) continue;

            if (!s_scriptTypesByHash.TryGetValue(comp.ScriptClassId, out Type? scriptType))
            {
                continue;
            }
            boundTypes.Add(scriptType);

            if (comp.ManagedGCHandle != 0ul) continue;

            if (Activator.CreateInstance(scriptType) is IMorbiScript instance)
            {
                EntityId entId = entities[i];
                ulong packed = entId.Pack();
                ulong token = s_nextInstanceToken++;
                comp.ManagedGCHandle = token;

                if (instance is Script scriptObj)
                {
                    scriptObj.BindEntity(new Entity(entId, NativeBridge.GetEntityName(entId)));
                    RegisterEntityScript(packed, scriptObj);
                }
                else
                {
                    s_activeScripts.Add(instance);
                }

                try
                {
                    instance.OnStart();
                }
                catch (Exception ex)
                {
                    NativeBridge.ReportFatalException(ex, $"EntityScript '{scriptType.Name}'.OnStart");
                }
            }
        }
        return boundTypes;
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int LoadCollectibleAssembly(byte* utf8Path)
    {
        return LoadCollectibleAssemblyInternal(Marshal.PtrToStringUTF8((IntPtr)utf8Path));
    }

    private static int LoadCollectibleAssemblyInternal(string? path)
    {
        try
        {
            if (string.IsNullOrWhiteSpace(path) || !File.Exists(path))
            {
                return -1;
            }

            UnloadActiveContextInternal();

            s_reloadCount++;
            s_activeAlc = new CollectibleScriptLoadContext($"MorbiHotReloadALC_{s_reloadCount}");
            s_alcWeakRef = new WeakReference(s_activeAlc, trackResurrection: true);

            // Load from MemoryStream so the DLL file on disk is NEVER locked by Windows!
            byte[] dllBytes = File.ReadAllBytes(path);
            using var ms = new MemoryStream(dllBytes);
            Assembly loadedAsm = s_activeAlc.LoadFromStream(ms);

            var exportedScriptTypes = new List<Type>();
            s_scriptTypesByHash.Clear();
            foreach (Type t in loadedAsm.GetExportedTypes())
            {
                if (!t.IsAbstract && typeof(IMorbiScript).IsAssignableFrom(t))
                {
                    exportedScriptTypes.Add(t);
                    if (!string.IsNullOrEmpty(t.FullName))
                    {
                        s_scriptTypesByHash[ComputeStringId(t.FullName)] = t;
                    }
                    s_scriptTypesByHash.TryAdd(ComputeStringId(t.Name), t);
                }
            }

            // First instantiate any entity-bound ScriptComponents referencing types in this assembly
            HashSet<Type> entityBoundTypes = SyncEntityScriptsFromArena();

            // Then instantiate singleton/global scripts that do not carry [EntityScript] and weren't claimed by a ScriptComponent
            s_activeScripts.Clear();
            for (int i = 0; i < exportedScriptTypes.Count; i++)
            {
                Type t = exportedScriptTypes[i];
                if (entityBoundTypes.Contains(t) || t.IsDefined(typeof(EntityScriptAttribute), inherit: false))
                {
                    continue;
                }
                if (Activator.CreateInstance(t) is IMorbiScript instance)
                {
                    instance.OnStart();
                    s_activeScripts.Add(instance);
                }
            }

            NativeBridge.LogInfo("C#.HotReload",
                $"Loaded '{Path.GetFileName(path)}' in Collectible AssemblyLoadContext (Gen={s_reloadCount}, Global={s_activeScripts.Count}, Entities={s_entityScripts.Count}).");
            return 0;
        }
        catch (Exception ex)
        {
            NativeBridge.ReportFatalException(ex, "LoadCollectibleAssembly");
            return -99;
        }
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int CompileAndLoadScripts(
        byte* scriptsDirUtf8,
        byte* outputDllUtf8,
        int loadAfterCompile,
        int releaseOptimize,
        byte* errorBufUtf8,
        int errorBufSize,
        int* outFileCount)
    {
        try
        {
            if (outFileCount != null) *outFileCount = 0;
            if (errorBufUtf8 != null && errorBufSize > 0) errorBufUtf8[0] = 0;

            string? scriptsDir = Marshal.PtrToStringUTF8((IntPtr)scriptsDirUtf8);
            string? outputDll = Marshal.PtrToStringUTF8((IntPtr)outputDllUtf8);
            if (string.IsNullOrWhiteSpace(outputDll))
            {
                return -1;
            }

            ScriptCompileOutcome outcome = ScriptCompiler.CompileDirectory(
                scriptsDir ?? string.Empty,
                outputDll,
                releaseOptimize != 0);

            if (outFileCount != null) *outFileCount = outcome.FileCount;

            if (!outcome.Success)
            {
                WriteUtf8Buffer(outcome.Diagnostics, errorBufUtf8, errorBufSize);
                NativeBridge.LogInfo("C#.Compiler", $"Compilation failed ({outcome.FileCount} files, {outcome.ElapsedMs} ms):\n{outcome.Diagnostics}");
                return -10;
            }

            if (outcome.FileCount == 0)
            {
                if (loadAfterCompile != 0)
                {
                    UnloadActiveContextInternal();
                }
                NativeBridge.LogInfo("C#.Compiler", "No .cs scripts in active project; cleared active script context.");
                return 1;
            }

            NativeBridge.LogInfo("C#.Compiler",
                $"Compiled {outcome.FileCount} script(s) -> '{Path.GetFileName(outputDll)}' in {outcome.ElapsedMs} ms.");

            if (loadAfterCompile != 0)
            {
                return LoadCollectibleAssemblyInternal(outputDll);
            }

            return 0;
        }
        catch (Exception ex)
        {
            WriteUtf8Buffer(ex.ToString(), errorBufUtf8, errorBufSize);
            NativeBridge.ReportFatalException(ex, "CompileAndLoadScripts");
            return -99;
        }
    }

    private static void WriteUtf8Buffer(string text, byte* buffer, int bufferSize)
    {
        if (buffer == null || bufferSize <= 0 || string.IsNullOrEmpty(text)) return;
        byte[] bytes = Encoding.UTF8.GetBytes(text);
        int copyLen = Math.Min(bytes.Length, bufferSize - 1);
        for (int i = 0; i < copyLen; i++)
        {
            buffer[i] = bytes[i];
        }
        buffer[copyLen] = 0;
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int UnloadCollectibleAssembly()
    {
        try
        {
            UnloadActiveContextInternal();
            return 0;
        }
        catch (Exception ex)
        {
            NativeBridge.LogInfo("C#.HotReload", $"UnloadCollectibleAssembly exception: {ex.Message}");
            return -99;
        }
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int TickScripts(float deltaTime)
    {
        try
        {
            SyncEntityScriptsFromArena();

            for (int i = 0; i < s_activeScripts.Count; i++)
            {
                try
                {
                    s_activeScripts[i].OnUpdate(deltaTime);
                }
                catch (Exception ex)
                {
                    NativeBridge.ReportFatalException(ex, $"Script '{s_activeScripts[i].GetType().Name}'.OnUpdate");
                }
            }

            if (s_entityScripts.Count > 0)
            {
                s_tickSnapshot.Clear();
                foreach (var kv in s_entityScripts)
                {
                    for (int j = 0; j < kv.Value.Count; j++)
                    {
                        s_tickSnapshot.Add(kv.Value[j]);
                    }
                }
                for (int i = 0; i < s_tickSnapshot.Count; i++)
                {
                    Script script = s_tickSnapshot[i];
                    if (script.Entity == null || !script.Entity.IsAlive) continue;
                    try
                    {
                        script.OnUpdate(deltaTime);
                    }
                    catch (Exception ex)
                    {
                        NativeBridge.ReportFatalException(ex, $"EntityScript '{script.GetType().Name}'.OnUpdate");
                    }
                }
                s_tickSnapshot.Clear();
            }
            return 0;
        }
        catch (Exception ex)
        {
            NativeBridge.ReportFatalException(ex, "TickScripts");
            return -99;
        }
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int TickFixedScripts(float fixedDeltaTime)
    {
        try
        {
            SyncEntityScriptsFromArena();

            for (int i = 0; i < s_activeScripts.Count; i++)
            {
                if (s_activeScripts[i] is Script script)
                {
                    try
                    {
                        script.OnFixedUpdate(fixedDeltaTime);
                    }
                    catch (Exception ex)
                    {
                        NativeBridge.ReportFatalException(ex, $"Script '{script.GetType().Name}'.OnFixedUpdate");
                    }
                }
            }

            if (s_entityScripts.Count > 0)
            {
                s_tickSnapshot.Clear();
                foreach (var kv in s_entityScripts)
                {
                    for (int j = 0; j < kv.Value.Count; j++)
                    {
                        s_tickSnapshot.Add(kv.Value[j]);
                    }
                }
                for (int i = 0; i < s_tickSnapshot.Count; i++)
                {
                    Script script = s_tickSnapshot[i];
                    if (script.Entity == null || !script.Entity.IsAlive) continue;
                    try
                    {
                        script.OnFixedUpdate(fixedDeltaTime);
                    }
                    catch (Exception ex)
                    {
                        NativeBridge.ReportFatalException(ex, $"EntityScript '{script.GetType().Name}'.OnFixedUpdate");
                    }
                }
                s_tickSnapshot.Clear();
            }
            return 0;
        }
        catch (Exception ex)
        {
            NativeBridge.ReportFatalException(ex, "TickFixedScripts");
            return -99;
        }
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int DispatchPhysicsEvents(NativeCollisionEvent* events, int count)
    {
        try
        {
            if (events == null || count <= 0 || s_entityScripts.Count == 0)
            {
                return 0;
            }

            for (int i = 0; i < count; i++)
            {
                NativeCollisionEvent ev = events[i];
                bool isTrigger = ev.IsTrigger != 0;

                if (s_entityScripts.TryGetValue(ev.EntityAPacked, out List<Script>? listA))
                {
                    EntityId otherId = EntityId.Unpack(ev.EntityBPacked);
                    DispatchToEntityList(
                        listA,
                        otherId,
                        ev.ContactPoint,
                        ev.ContactNormal,
                        ev.RelativeVelocity,
                        ev.PenetrationDepth,
                        (ColliderShape)ev.ShapeA,
                        (ColliderShape)ev.ShapeB,
                        ev.EventType,
                        isTrigger);
                }

                if (s_entityScripts.TryGetValue(ev.EntityBPacked, out List<Script>? listB))
                {
                    EntityId otherId = EntityId.Unpack(ev.EntityAPacked);
                    DispatchToEntityList(
                        listB,
                        otherId,
                        ev.ContactPoint,
                        -ev.ContactNormal,
                        -ev.RelativeVelocity,
                        ev.PenetrationDepth,
                        (ColliderShape)ev.ShapeB,
                        (ColliderShape)ev.ShapeA,
                        ev.EventType,
                        isTrigger);
                }
            }
            return 0;
        }
        catch (Exception ex)
        {
            NativeBridge.ReportFatalException(ex, "DispatchPhysicsEvents");
            return -99;
        }
    }

    private static void DispatchToEntityList(
        List<Script> scripts,
        EntityId otherEntityId,
        in Vec3 contactPoint,
        in Vec3 contactNormal,
        in Vec3 relativeVelocity,
        float penetrationDepth,
        ColliderShape selfShape,
        ColliderShape otherShape,
        byte eventType,
        bool isTrigger)
    {
        int snapshotCount = scripts.Count;
        for (int s = 0; s < snapshotCount; s++)
        {
            Script script = scripts[s];
            try
            {
                if (isTrigger)
                {
                    var otherCollider = new Collider(otherEntityId);
                    switch (eventType)
                    {
                        case 0: script.OnTriggerEnter(otherCollider); break;
                        case 1: script.OnTriggerStay(otherCollider); break;
                        case 2: script.OnTriggerExit(otherCollider); break;
                    }
                }
                else
                {
                    var collision = new Collision(
                        otherEntityId,
                        contactPoint,
                        contactNormal,
                        relativeVelocity,
                        penetrationDepth,
                        selfShape,
                        otherShape,
                        isTrigger: false);
                    switch (eventType)
                    {
                        case 0: script.OnCollisionEnter(collision); break;
                        case 1: script.OnCollisionStay(collision); break;
                        case 2: script.OnCollisionExit(collision); break;
                    }
                }
            }
            catch (Exception ex)
            {
                NativeBridge.ReportFatalException(ex, $"Physics callback on '{script.GetType().Name}'");
            }
        }
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int NotifyEntityDestroyed(ulong packedEntityId)
    {
        try
        {
            if (s_entityScripts.Remove(packedEntityId, out List<Script>? scripts))
            {
                for (int i = 0; i < scripts.Count; i++)
                {
                    try
                    {
                        scripts[i].OnDestroy();
                    }
                    catch (Exception ex)
                    {
                        NativeBridge.ReportFatalException(ex, $"OnDestroy on '{scripts[i].GetType().Name}'");
                    }
                }
            }
            return 0;
        }
        catch (Exception ex)
        {
            NativeBridge.ReportFatalException(ex, "NotifyEntityDestroyed");
            return -99;
        }
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int NotifySceneCleared()
    {
        try
        {
            s_tickSnapshot.Clear();
            foreach (var kv in s_entityScripts)
            {
                for (int i = 0; i < kv.Value.Count; i++)
                {
                    try { kv.Value[i].OnDestroy(); } catch { }
                }
            }
            s_entityScripts.Clear();

            Span<ScriptComponent> scriptComponents = NativeBridge.GetScriptSpan(out _);
            for (int i = 0; i < scriptComponents.Length; i++)
            {
                scriptComponents[i].ManagedGCHandle = 0ul;
            }
            return 0;
        }
        catch
        {
            return -99;
        }
    }

    [MethodImpl(MethodImplOptions.NoInlining)]
    private static void UnloadActiveContextInternal()
    {
        s_tickSnapshot.Clear();

        for (int i = 0; i < s_activeScripts.Count; i++)
        {
            if (s_activeScripts[i] is Script script)
            {
                try { script.OnDestroy(); } catch { }
            }
        }
        foreach (var kv in s_entityScripts)
        {
            for (int i = 0; i < kv.Value.Count; i++)
            {
                try { kv.Value[i].OnDestroy(); } catch { }
            }
        }

        // Clear native ScriptComponent handles so hot-reloading re-instantiates cleanly
        Span<ScriptComponent> scriptComponents = NativeBridge.GetScriptSpan(out _);
        for (int i = 0; i < scriptComponents.Length; i++)
        {
            scriptComponents[i].ManagedGCHandle = 0ul;
        }

        s_activeScripts.Clear();
        s_entityScripts.Clear();
        s_scriptTypesByHash.Clear();
        NetworkWorldRegistry.ClearAll();

        if (s_activeAlc != null)
        {
            s_activeAlc.Unload();
            s_activeAlc = null;
        }

        if (s_alcWeakRef != null)
        {
            for (int i = 0; s_alcWeakRef.IsAlive && i < 10; i++)
            {
                GC.Collect();
                GC.WaitForPendingFinalizers();
            }
            NativeBridge.LogInfo("C#.HotReload",
                $"Unloaded previous Collectible AssemblyLoadContext (Reclaimed={!s_alcWeakRef.IsAlive}).");
            s_alcWeakRef = null;
        }
    }

    // ------------------------------------------------------------------------
    // Phase 8: C++ -> C# Multi-World & Gameplay Networking Entry Points
    // ------------------------------------------------------------------------

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int NotifyNetworkSpawn(
        uint worldId,
        ulong netIdPacked,
        ulong sceneEntityPacked,
        ulong ownerPlayerId,
        byte isServer,
        byte isClient,
        byte isOwner)
    {
        try
        {
            s_entityScripts.TryGetValue(sceneEntityPacked, out List<Script>? scripts);
            NetworkWorldRegistry.OnNetworkSpawn(
                worldId,
                netIdPacked,
                sceneEntityPacked,
                ownerPlayerId,
                isServer != 0,
                isClient != 0,
                isOwner != 0,
                scripts);
            return 0;
        }
        catch (Exception ex)
        {
            NativeBridge.ReportFatalException(ex, "ManagedHost.NotifyNetworkSpawn");
            return -1;
        }
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int NotifyNetworkDespawn(uint worldId, ulong netIdPacked)
    {
        try
        {
            NetworkWorldRegistry.OnNetworkDespawn(worldId, netIdPacked);
            return 0;
        }
        catch (Exception ex)
        {
            NativeBridge.ReportFatalException(ex, "ManagedHost.NotifyNetworkDespawn");
            return -1;
        }
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int NotifyNetworkOwnershipChanged(
        uint worldId,
        ulong netIdPacked,
        ulong prevOwner,
        ulong newOwner,
        byte isOwner)
    {
        try
        {
            NetworkWorldRegistry.OnNetworkOwnershipChanged(worldId, netIdPacked, prevOwner, newOwner, isOwner != 0);
            return 0;
        }
        catch (Exception ex)
        {
            NativeBridge.ReportFatalException(ex, "ManagedHost.NotifyNetworkOwnershipChanged");
            return -1;
        }
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int NotifyNetworkWorldChanged(uint prevWorldId, uint newWorldId, ulong netIdPacked)
    {
        try
        {
            NetworkWorldRegistry.OnNetworkWorldChanged(prevWorldId, newWorldId, netIdPacked);
            return 0;
        }
        catch (Exception ex)
        {
            NativeBridge.ReportFatalException(ex, "ManagedHost.NotifyNetworkWorldChanged");
            return -1;
        }
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int NotifyWorldDestroyed(uint worldId)
    {
        try
        {
            NetworkWorldRegistry.OnWorldDestroyed(worldId);
            return 0;
        }
        catch (Exception ex)
        {
            NativeBridge.ReportFatalException(ex, "ManagedHost.NotifyWorldDestroyed");
            return -1;
        }
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int DispatchManagedPropertySync(
        uint worldId,
        ulong netIdPacked,
        uint behaviourHash,
        byte* propsBytes,
        uint propsLen)
    {
        try
        {
            if (propsBytes == null || propsLen == 0) return -1;
            var span = new ReadOnlySpan<byte>(propsBytes, (int)propsLen);
            return NetworkWorldRegistry.DispatchPropertySync(worldId, netIdPacked, behaviourHash, span) ? 0 : -1;
        }
        catch (Exception ex)
        {
            NativeBridge.ReportFatalException(ex, "ManagedHost.DispatchManagedPropertySync");
            return -1;
        }
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int DispatchManagedRpc(
        uint worldId,
        ulong netIdPacked,
        uint behaviourHash,
        uint methodHash,
        ulong senderPlayerId,
        byte isServerSide,
        byte* argsBytes,
        uint argsLen)
    {
        try
        {
            _ = senderPlayerId;
            ReadOnlySpan<byte> span = (argsBytes != null && argsLen > 0)
                ? new ReadOnlySpan<byte>(argsBytes, (int)argsLen)
                : ReadOnlySpan<byte>.Empty;
            return NetworkWorldRegistry.DispatchRpc(
                worldId,
                netIdPacked,
                behaviourHash,
                methodHash,
                isServerSide != 0,
                span) ? 0 : -1;
        }
        catch (Exception ex)
        {
            NativeBridge.ReportFatalException(ex, "ManagedHost.DispatchManagedRpc");
            return -1;
        }
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int SyncManagedNetworkProperties(uint worldId)
    {
        try
        {
            NetworkWorldRegistry.SyncDirtyPropertiesForWorld(worldId);
            return 0;
        }
        catch (Exception ex)
        {
            NativeBridge.ReportFatalException(ex, "ManagedHost.SyncManagedNetworkProperties");
            return -1;
        }
    }
}
