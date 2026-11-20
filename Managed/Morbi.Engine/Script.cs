#nullable enable
using System;

namespace Morbi.Engine;

[AttributeUsage(AttributeTargets.Class, Inherited = false)]
public sealed class EntityScriptAttribute : Attribute
{
}

public interface IMorbiScript
{
    void OnStart();
    void OnUpdate(float deltaTime);
}

public abstract class Script : IMorbiScript
{
    public Entity? Entity { get; private set; }

    internal void BindEntity(Entity? entity) => Entity = entity;

    protected Transform? Transform => Entity?.GetTransform();
    protected RigidBody? RigidBody => Entity?.GetRigidBody();

    protected T? GetComponent<T>() where T : class => Entity?.GetComponent<T>();
    protected T? AddComponent<T>() where T : class => Entity?.AddComponent<T>();

    public virtual void OnCreate() { }
    public virtual void OnStart() => OnCreate();
    public virtual void OnUpdate(float deltaTime) { }
    public virtual void OnFixedUpdate(float fixedDeltaTime) { }
    public virtual void OnDestroy() { }

    public virtual void OnCollisionEnter(Collision collision) { }
    public virtual void OnCollisionStay(Collision collision) { }
    public virtual void OnCollisionExit(Collision collision) { }

    public virtual void OnTriggerEnter(Collider other) { }
    public virtual void OnTriggerStay(Collider other) { }
    public virtual void OnTriggerExit(Collider other) { }
}
