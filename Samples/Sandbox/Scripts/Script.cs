using System;

namespace Morbi.Engine;

public interface IMorbiScript
{
    void OnStart();
    void OnUpdate(float deltaTime);
}

public abstract class Script : IMorbiScript
{
    public virtual void OnCreate() { }
    public virtual void OnStart() => OnCreate();
    public virtual void OnUpdate(float deltaTime) { }
}
