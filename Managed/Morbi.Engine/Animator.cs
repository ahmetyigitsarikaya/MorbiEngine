using System;

namespace Morbi.Engine;

public sealed class Animator
{
    private readonly EntityId _entityId;
    private string _currentClip = "";
    private float _playbackSpeed = 1.0f;

    internal Animator(EntityId entityId)
    {
        _entityId = entityId;
    }

    public EntityId EntityId => _entityId;

    public bool IsPlaying => NativeBridge.IsAnimationPlaying(_entityId);

    public bool IsBlending => NativeBridge.IsAnimationBlending(_entityId);

    public string CurrentClip
    {
        get => _currentClip;
        set => SetClip(value);
    }

    public float PlaybackSpeed
    {
        get => _playbackSpeed;
        set
        {
            _playbackSpeed = value;
            NativeBridge.SetAnimationSpeed(_entityId, value);
        }
    }

    public bool Play()
    {
        return NativeBridge.PlayAnimation(_entityId);
    }

    public bool Pause()
    {
        return NativeBridge.PauseAnimation(_entityId);
    }

    public bool Stop()
    {
        return NativeBridge.StopAnimation(_entityId);
    }

    public bool SetClip(string clipPath)
    {
        if (string.IsNullOrWhiteSpace(clipPath)) return false;
        _currentClip = clipPath;
        return NativeBridge.SetAnimationClip(_entityId, clipPath);
    }

    public bool CrossFade(string clipPath, float blendDurationSeconds = 0.15f)
    {
        if (string.IsNullOrWhiteSpace(clipPath)) return false;
        _currentClip = clipPath;
        return NativeBridge.CrossFadeAnimationClip(_entityId, clipPath, blendDurationSeconds);
    }
}

// Convenient alias matching engine ECS nomenclature
public sealed class AnimatorComponent
{
    private readonly Animator _inner;

    internal AnimatorComponent(Animator inner) => _inner = inner;

    public bool IsPlaying => _inner.IsPlaying;
    public bool IsBlending => _inner.IsBlending;
    public string CurrentClip { get => _inner.CurrentClip; set => _inner.CurrentClip = value; }
    public float PlaybackSpeed { get => _inner.PlaybackSpeed; set => _inner.PlaybackSpeed = value; }
    public bool Play() => _inner.Play();
    public bool Pause() => _inner.Pause();
    public bool Stop() => _inner.Stop();
    public bool SetClip(string clipPath) => _inner.SetClip(clipPath);
    public bool CrossFade(string clipPath, float blendDurationSeconds = 0.15f) => _inner.CrossFade(clipPath, blendDurationSeconds);
}
