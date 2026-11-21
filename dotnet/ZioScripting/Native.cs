using System;
using System.Runtime.InteropServices;
using System.Text;

namespace Zio
{

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct NativeApi
{
    public delegate* unmanaged<IntPtr, float*, void> getPosition;
    public delegate* unmanaged<IntPtr, float*, void> setPosition;
    public delegate* unmanaged<IntPtr, float*, void> getRotation;
    public delegate* unmanaged<IntPtr, float*, void> setRotation;
    public delegate* unmanaged<IntPtr, float*, void> getScale;
    public delegate* unmanaged<IntPtr, float*, void> setScale;
    public delegate* unmanaged<IntPtr, byte*, int, int> getName;
    public delegate* unmanaged<byte*, int, int, void> log;
    public delegate* unmanaged<int, int> isKeyDown;
    public delegate* unmanaged<int, int> isMouseDown;
    public delegate* unmanaged<float*, void> getMousePosition;
}

internal static class Native
{
    internal static NativeApi Api;
}

public static unsafe class Log
{
    public static void Info(string message)
    {
        Write(message, 0);
    }

    public static void Error(string message)
    {
        Write(message, 1);
    }

    internal static void Write(string message, int level)
    {
        byte[] bytes = Encoding.UTF8.GetBytes(message ?? string.Empty);
        fixed (byte* p = bytes)
        {
            Native.Api.log(p, bytes.Length, level);
        }
    }
}

public static unsafe class Input
{
    public static bool IsKeyDown(Key key)
    {
        return Native.Api.isKeyDown((int)key) != 0;
    }

    public static bool IsMouseDown(MouseButton button)
    {
        return Native.Api.isMouseDown((int)button) != 0;
    }

    public static System.Numerics.Vector2 MousePosition
    {
        get
        {
            float* v = stackalloc float[2];
            Native.Api.getMousePosition(v);
            return new System.Numerics.Vector2(v[0], v[1]);
        }
    }
}

}
