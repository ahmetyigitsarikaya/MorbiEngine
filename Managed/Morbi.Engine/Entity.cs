using System;

namespace Morbi.Engine;

public sealed class Entity
{
    private readonly EntityId _id;
    private readonly string _name;
    private Transform? _transform;
    private Animator? _animator;
    private AnimatorComponent? _animatorComponent;
    private Camera? _camera;
    private Light? _light;
    private MeshRenderer? _meshRenderer;
    private RigidBody? _rigidBody;
    private Collider? _collider;
    private BoxCollider? _boxCollider;
    private SphereCollider? _sphereCollider;
    private CapsuleCollider? _capsuleCollider;
    private CompoundCollider? _compoundCollider;
    private AudioSource? _audioSource;
    private CharacterController? _characterController;

    public Entity(EntityId id, string name = "")
    {
        _id = id;
        _name = name;
    }

    public EntityId Id => _id;
    public uint Index => _id.Index;
    public uint Generation => _id.Generation;

    public string Name
    {
        get
        {
            string liveName = NativeBridge.GetEntityName(_id);
            return !string.IsNullOrEmpty(liveName) ? liveName : _name;
        }
        set
        {
            if (_id.IsValid)
            {
                NativeBridge.SetEntityName(_id, value ?? string.Empty);
            }
        }
    }

    public Entity? Parent
    {
        get
        {
            EntityId parentId = NativeBridge.GetEntityParent(_id);
            return parentId.IsValid ? new Entity(parentId, NativeBridge.GetEntityName(parentId)) : null;
        }
    }

    public bool IsValid => _id.IsValid;
    public bool IsAlive => NativeBridge.IsEntityAlive(_id);

    public Transform GetTransform() => _transform ??= new Transform(_id);
    public Animator GetAnimator() => _animator ??= new Animator(_id);
    public Camera? GetCamera() => HasComponent<Camera>() ? (_camera ??= new Camera(_id)) : null;
    public Light? GetLight() => HasComponent<Light>() ? (_light ??= new Light(_id)) : null;
    public MeshRenderer? GetMeshRenderer() => HasComponent<MeshRenderer>() ? (_meshRenderer ??= new MeshRenderer(_id)) : null;
    public RigidBody? GetRigidBody() => HasComponent<RigidBody>() ? (_rigidBody ??= new RigidBody(_id)) : null;
    public CompoundCollider? GetCompoundCollider() => HasComponent<CompoundCollider>() ? (_compoundCollider ??= new CompoundCollider(_id)) : null;
    public AudioSource? GetAudioSource() => HasComponent<AudioSource>() ? (_audioSource ??= new AudioSource(_id)) : null;
    public CharacterController? GetCharacterController() => HasComponent<CharacterController>() ? (_characterController ??= new CharacterController(_id)) : null;

    public bool HasComponent<T>() where T : class =>
        TryResolvePoolKind<T>(out ComponentPoolKind kind) && NativeBridge.HasComponent(_id, kind);

    public T? GetComponent<T>() where T : class
    {
        if (!TryResolvePoolKind<T>(out ComponentPoolKind kind) || !NativeBridge.HasComponent(_id, kind))
        {
            return null;
        }
        return ResolveWrapper<T>();
    }

    public T? AddComponent<T>() where T : class
    {
        if (!TryResolvePoolKind<T>(out ComponentPoolKind kind) || !NativeBridge.AddComponent(_id, kind))
        {
            return null;
        }
        return ResolveWrapper<T>();
    }

    public bool RemoveComponent<T>() where T : class =>
        TryResolvePoolKind<T>(out ComponentPoolKind kind) && NativeBridge.RemoveComponent(_id, kind);

    private T? ResolveWrapper<T>() where T : class
    {
        if (typeof(T) == typeof(Transform)) return GetTransform() as T;
        if (typeof(T) == typeof(Animator)) return GetAnimator() as T;
        if (typeof(T) == typeof(AnimatorComponent)) return (_animatorComponent ??= new AnimatorComponent(GetAnimator())) as T;
        if (typeof(T) == typeof(Camera)) return (_camera ??= new Camera(_id)) as T;
        if (typeof(T) == typeof(Light)) return (_light ??= new Light(_id)) as T;
        if (typeof(T) == typeof(MeshRenderer)) return (_meshRenderer ??= new MeshRenderer(_id)) as T;
        if (typeof(T) == typeof(RigidBody)) return (_rigidBody ??= new RigidBody(_id)) as T;
        if (typeof(T) == typeof(BoxCollider)) return (_boxCollider ??= new BoxCollider(_id)) as T;
        if (typeof(T) == typeof(SphereCollider)) return (_sphereCollider ??= new SphereCollider(_id)) as T;
        if (typeof(T) == typeof(CapsuleCollider)) return (_capsuleCollider ??= new CapsuleCollider(_id)) as T;
        if (typeof(T) == typeof(CompoundCollider)) return (_compoundCollider ??= new CompoundCollider(_id)) as T;
        if (typeof(T) == typeof(Collider)) return (_collider ??= new Collider(_id)) as T;
        if (typeof(T) == typeof(AudioSource)) return (_audioSource ??= new AudioSource(_id)) as T;
        if (typeof(T) == typeof(CharacterController)) return (_characterController ??= new CharacterController(_id)) as T;
        return null;
    }

    private static bool TryResolvePoolKind<T>(out ComponentPoolKind kind) where T : class
    {
        if (typeof(T) == typeof(Transform)) { kind = ComponentPoolKind.Transform; return true; }
        if (typeof(T) == typeof(Animator) || typeof(T) == typeof(AnimatorComponent)) { kind = ComponentPoolKind.Animator; return true; }
        if (typeof(T) == typeof(Camera)) { kind = ComponentPoolKind.Camera; return true; }
        if (typeof(T) == typeof(Light)) { kind = ComponentPoolKind.Light; return true; }
        if (typeof(T) == typeof(MeshRenderer)) { kind = ComponentPoolKind.MeshRenderer; return true; }
        if (typeof(T) == typeof(RigidBody) ||
            typeof(T) == typeof(Collider) ||
            typeof(T) == typeof(BoxCollider) ||
            typeof(T) == typeof(SphereCollider) ||
            typeof(T) == typeof(CapsuleCollider) ||
            typeof(T) == typeof(CompoundCollider))
        {
            kind = ComponentPoolKind.RigidBody;
            return true;
        }
        if (typeof(T) == typeof(AudioSource)) { kind = ComponentPoolKind.AudioSource; return true; }
        if (typeof(T) == typeof(CharacterController)) { kind = ComponentPoolKind.CharacterController; return true; }
        kind = default;
        return false;
    }

    public T? AttachScript<T>() where T : Script, new()
    {
        if (!IsValid || !IsAlive) return null;
        return ManagedHost.AttachScript<T>(this);
    }

    public T? GetScript<T>() where T : Script => ManagedHost.FindEntityScript<T>(_id);

    public void SetParent(Entity? parent)
    {
        NativeBridge.SetEntityParent(_id, parent?.Id ?? EntityId.Invalid);
    }

    public void Destroy()
    {
        NativeBridge.DestroyEntity(_id);
    }

    public static Entity? Find(string name) => Scene.FindEntity(name);
}
