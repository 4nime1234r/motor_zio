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
    public delegate* unmanaged<IntPtr, int, byte*, int, IntPtr> createNode;
    public delegate* unmanaged<IntPtr, void> destroyNode;
    public delegate* unmanaged<IntPtr, byte*, int, IntPtr> findChild;
    public delegate* unmanaged<IntPtr, IntPtr> getParent;
    public delegate* unmanaged<IntPtr, int> getChildCount;
    public delegate* unmanaged<IntPtr, int, IntPtr> getChild;
    public delegate* unmanaged<IntPtr, int, void> setVisible;
    public delegate* unmanaged<IntPtr, int> isVisible;
    public delegate* unmanaged<IntPtr, byte*, int, void> setName;
    public delegate* unmanaged<IntPtr, byte*, int, int> setMesh;
    public delegate* unmanaged<IntPtr, float*, void> setColor;
    public delegate* unmanaged<IntPtr, byte*, int, void> setTexture;
    public delegate* unmanaged<IntPtr, float, float, void> setLight;
    public delegate* unmanaged<int, int, float*, float*, float, float, float, uint> createBody;
    public delegate* unmanaged<uint, void> destroyBody;
    public delegate* unmanaged<uint, int> hasBody;
    public delegate* unmanaged<uint, int, float*, void> getBodyVector;
    public delegate* unmanaged<uint, int, float*, void> setBodyVector;
    public delegate* unmanaged<uint, int, float*, void> applyBody;
    public delegate* unmanaged<float*, void> getGravity;
    public delegate* unmanaged<float*, void> setGravity;
    public delegate* unmanaged<float*, float*, float, float*, uint*, int> raycast;
    public delegate* unmanaged<int> contactCount;
    public delegate* unmanaged<int, uint*, uint*, float*, int> getContact;
    public delegate* unmanaged<byte*, int, float, float, int, int, uint> playSound;
    public delegate* unmanaged<uint, void> stopSound;
    public delegate* unmanaged<uint, int, float, void> setSound;
    public delegate* unmanaged<uint, int> isSoundPlaying;
    public delegate* unmanaged<int, float, void> setCategoryVolume;
    public delegate* unmanaged<byte*, int, void> loadScene;
    public delegate* unmanaged<IntPtr, byte*, int, IntPtr> instantiate;
    public delegate* unmanaged<uint, uint, uint, int, void> setBodyFilter;
    public delegate* unmanaged<int, uint, uint, float*, float*, float, uint> createJoint;
    public delegate* unmanaged<uint, void> destroyJoint;
    public delegate* unmanaged<int> triggerEventCount;
    public delegate* unmanaged<int, uint*, uint*, int*, int> getTriggerEvent;
    public delegate* unmanaged<IntPtr, uint> createMeshBody;
    public delegate* unmanaged<byte*, int, float, float, int, uint, int, int, float*, float, float, uint> playSoundEx;
    public delegate* unmanaged<float*, float*, float*, void> setListener;
    public delegate* unmanaged<uint, float*, void> setSoundPosition;
    public delegate* unmanaged<byte*, int, uint, uint> createBus;
    public delegate* unmanaged<byte*, int, uint> findBus;
    public delegate* unmanaged<uint, float, void> setBusVolume;
    public delegate* unmanaged<uint, int, float, int> setBusEffect;
    public delegate* unmanaged<uint, void> clearBusEffects;
}

internal static class Native
{
    internal static NativeApi Api;

    internal static unsafe T WithUtf8<T>(string text, Func<IntPtr, int, T> call)
    {
        byte[] bytes = Encoding.UTF8.GetBytes(text ?? string.Empty);
        fixed (byte* p = bytes)
        {
            return call((IntPtr)p, bytes.Length);
        }
    }
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
