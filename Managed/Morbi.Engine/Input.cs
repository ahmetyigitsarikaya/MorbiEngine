using System;

namespace Morbi.Engine;

public enum KeyCode : uint
{
    // Alphabet
    A = 4,
    B = 5,
    C = 6,
    D = 7,
    E = 8,
    F = 9,
    G = 10,
    H = 11,
    I = 12,
    J = 13,
    K = 14,
    L = 15,
    M = 16,
    N = 17,
    O = 18,
    P = 19,
    Q = 20,
    R = 21,
    S = 22,
    T = 23,
    U = 24,
    V = 25,
    W = 26,
    X = 27,
    Y = 28,
    Z = 29,

    // Numbers
    Alpha1 = 30,
    Alpha2 = 31,
    Alpha3 = 32,
    Alpha4 = 33,
    Alpha5 = 34,
    Alpha6 = 35,
    Alpha7 = 36,
    Alpha8 = 37,
    Alpha9 = 38,
    Alpha0 = 39,

    // Controls
    Return = 40,
    Escape = 41,
    Backspace = 42,
    Tab = 43,
    Space = 44,

    // Function keys
    F1 = 58,
    F2 = 59,
    F3 = 60,
    F4 = 61,
    F5 = 62,
    F6 = 63,
    F7 = 64,
    F8 = 65,
    F9 = 66,
    F10 = 67,
    F11 = 68,
    F12 = 69,

    // Navigation
    Right = 79,
    Left = 80,
    Down = 81,
    Up = 82,

    // Modifiers
    LCtrl = 224,
    LShift = 225,
    LAlt = 226,
    RCtrl = 228,
    RShift = 229,
    RAlt = 230
}

public enum MouseButton : byte
{
    Left = 0,
    Right = 1,
    Middle = 2,
    X1 = 3,
    X2 = 4,

    // Aliases for convenience in scripts
    Button1 = 0,
    Button2 = 1,
    Button3 = 2,
    Button4 = 3,
    Button5 = 4,
    Back = 3,
    Forward = 4,
    Count = 5
}

public enum MouseAxis : byte
{
    X = 0,       // Left (-) / Right (+)
    Y = 1,       // Up (-) / Down (+)
    Z = 2,       // Scroll Wheel Vertical (Up + / Down -)
    ScrollY = 2,
    ScrollX = 3  // Scroll Wheel Horizontal
}

public static class Input
{
    // =========================================================================
    // Keyboard Input
    // =========================================================================
    public static bool IsKeyDown(KeyCode key) => NativeBridge.IsKeyDown((uint)key);
    public static bool IsKeyPressed(KeyCode key) => NativeBridge.IsKeyDown((uint)key);
    public static bool WasKeyPressedThisFrame(KeyCode key) => NativeBridge.WasKeyPressed((uint)key);
    public static bool WasKeyReleasedThisFrame(KeyCode key) => NativeBridge.WasKeyReleased((uint)key);

    // =========================================================================
    // Mouse Buttons (Left, Right, Middle, X1/Button4/Back, X2/Button5/Forward)
    // =========================================================================
    public static bool IsMouseButtonDown(MouseButton button) => NativeBridge.IsMouseButtonDown((byte)button);
    public static bool IsMouseButtonDown(int buttonIndex) =>
        buttonIndex >= 0 && buttonIndex < (int)MouseButton.Count && NativeBridge.IsMouseButtonDown((byte)buttonIndex);

    public static bool WasMouseButtonPressed(MouseButton button) => NativeBridge.WasMouseButtonPressed((byte)button);
    public static bool WasMouseButtonPressedThisFrame(MouseButton button) => NativeBridge.WasMouseButtonPressed((byte)button);
    public static bool WasMouseButtonPressed(int buttonIndex) =>
        buttonIndex >= 0 && buttonIndex < (int)MouseButton.Count && NativeBridge.WasMouseButtonPressed((byte)buttonIndex);

    public static bool WasMouseButtonReleased(MouseButton button) => NativeBridge.WasMouseButtonReleased((byte)button);
    public static bool WasMouseButtonReleasedThisFrame(MouseButton button) => NativeBridge.WasMouseButtonReleased((byte)button);
    public static bool WasMouseButtonReleased(int buttonIndex) =>
        buttonIndex >= 0 && buttonIndex < (int)MouseButton.Count && NativeBridge.WasMouseButtonReleased((byte)buttonIndex);

    public static bool AnyMouseButtonDown
    {
        get
        {
            for (byte i = 0; i < (byte)MouseButton.Count; ++i)
            {
                if (NativeBridge.IsMouseButtonDown(i)) return true;
            }
            return false;
        }
    }

    public static bool AnyMouseButtonPressed
    {
        get
        {
            for (byte i = 0; i < (byte)MouseButton.Count; ++i)
            {
                if (NativeBridge.WasMouseButtonPressed(i)) return true;
            }
            return false;
        }
    }

    // Convenience button properties
    public static bool LeftMouseDown => IsMouseButtonDown(MouseButton.Left);
    public static bool RightMouseDown => IsMouseButtonDown(MouseButton.Right);
    public static bool MiddleMouseDown => IsMouseButtonDown(MouseButton.Middle);
    public static bool MouseButton4Down => IsMouseButtonDown(MouseButton.X1);
    public static bool MouseButton5Down => IsMouseButtonDown(MouseButton.X2);

    public static bool LeftMousePressed => WasMouseButtonPressed(MouseButton.Left);
    public static bool RightMousePressed => WasMouseButtonPressed(MouseButton.Right);
    public static bool MiddleMousePressed => WasMouseButtonPressed(MouseButton.Middle);
    public static bool MouseButton4Pressed => WasMouseButtonPressed(MouseButton.X1);
    public static bool MouseButton5Pressed => WasMouseButtonPressed(MouseButton.X2);

    public static bool LeftMouseReleased => WasMouseButtonReleased(MouseButton.Left);
    public static bool RightMouseReleased => WasMouseButtonReleased(MouseButton.Right);
    public static bool MiddleMouseReleased => WasMouseButtonReleased(MouseButton.Middle);
    public static bool MouseButton4Released => WasMouseButtonReleased(MouseButton.X1);
    public static bool MouseButton5Released => WasMouseButtonReleased(MouseButton.X2);

    // =========================================================================
    // Mouse Position & Movement Deltas (X: Left/Right, Y: Up/Down, Z: Scroll)
    // =========================================================================
    public static (float X, float Y) MousePosition
    {
        get
        {
            NativeBridge.GetMousePosition(out float x, out float y);
            return (x, y);
        }
    }

    /// <summary>
    /// 3D Mouse Position: X (screen X), Y (screen Y), Z (accumulated scroll wheel Z).
    /// </summary>
    public static Vec3 MousePosition3D
    {
        get
        {
            NativeBridge.GetMousePosition3D(out float x, out float y, out float z);
            return new Vec3(x, y, z);
        }
    }

    public static (float X, float Y) MouseDelta
    {
        get
        {
            NativeBridge.GetMouseDelta(out float dx, out float dy);
            return (dx, dy);
        }
    }

    /// <summary>
    /// 3D Mouse Frame Delta: X (Left/Right motion), Y (Up/Down motion), Z (Vertical Scroll Wheel delta).
    /// </summary>
    public static Vec3 MouseDelta3D
    {
        get
        {
            NativeBridge.GetMouseDelta3D(out float dx, out float dy, out float dz);
            return new Vec3(dx, dy, dz);
        }
    }

    public static float MouseX => MousePosition3D.X;
    public static float MouseY => MousePosition3D.Y;
    public static float MouseZ => MousePosition3D.Z;

    public static float MouseDeltaX => MouseDelta3D.X;
    public static float MouseDeltaY => MouseDelta3D.Y;
    public static float MouseDeltaZ => MouseDelta3D.Z;

    // =========================================================================
    // Mouse Scroll Wheel (Vertical Z & Horizontal X)
    // =========================================================================
    public static (float WheelX, float WheelY) MouseWheel
    {
        get
        {
            NativeBridge.GetMouseWheelDelta(out float wx, out float wy);
            return (wx, wy);
        }
    }

    /// <summary>
    /// Vertical scroll wheel delta this frame (+1 up / forward, -1 down / back).
    /// </summary>
    public static float MouseScroll
    {
        get
        {
            NativeBridge.GetMouseWheelDelta(out _, out float wy);
            return wy;
        }
    }

    public static float ScrollDelta => MouseScroll;

    /// <summary>
    /// Horizontal scroll wheel delta this frame.
    /// </summary>
    public static float MouseScrollX
    {
        get
        {
            NativeBridge.GetMouseWheelDelta(out float wx, out _);
            return wx;
        }
    }

    /// <summary>
    /// Queries any mouse axis delta by enum (X = Left/Right, Y = Up/Down, Z = Scroll).
    /// </summary>
    public static float GetMouseAxis(MouseAxis axis)
    {
        return axis switch
        {
            MouseAxis.X => MouseDeltaX,
            MouseAxis.Y => MouseDeltaY,
            MouseAxis.Z => MouseDeltaZ,
            MouseAxis.ScrollX => MouseScrollX,
            _ => 0.0f
        };
    }

    // =========================================================================
    // Mouse Capture / Lock & Cursor Visibility
    // =========================================================================
    /// <summary>
    /// Locks/captures the mouse to the game viewport for unlimited 360-degree look (FPS / Third-Person orbit).
    /// </summary>
    public static bool MouseCaptured
    {
        get => NativeBridge.IsMouseCaptured();
        set => NativeBridge.SetMouseCaptured(value);
    }

    /// <summary>
    /// Controls whether the OS mouse cursor is visible.
    /// </summary>
    public static bool CursorVisible
    {
        get => NativeBridge.IsCursorVisible();
        set => NativeBridge.SetCursorVisible(value);
    }

    public static void CaptureMouse(bool hideCursor = true)
    {
        NativeBridge.SetMouseCaptured(true);
        if (hideCursor)
        {
            NativeBridge.SetCursorVisible(false);
        }
    }

    public static void ReleaseMouse()
    {
        NativeBridge.SetMouseCaptured(false);
        NativeBridge.SetCursorVisible(true);
    }

    public static void ToggleMouseCapture()
    {
        if (MouseCaptured)
        {
            ReleaseMouse();
        }
        else
        {
            CaptureMouse(true);
        }
    }
}
