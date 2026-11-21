using System;
using System.Numerics;
using System.Text;

namespace Zio
{

public sealed unsafe class Node
{
    private readonly IntPtr handle;

    internal Node(IntPtr handle)
    {
        this.handle = handle;
    }

    public bool IsValid => handle != IntPtr.Zero;

    public string Name
    {
        get
        {
            if (handle == IntPtr.Zero)
            {
                return string.Empty;
            }
            byte* buffer = stackalloc byte[256];
            int length = Native.Api.getName(handle, buffer, 256);
            return length <= 0 ? string.Empty : Encoding.UTF8.GetString(buffer, Math.Min(length, 255));
        }
    }

    public Vector3 Position
    {
        get
        {
            float* v = stackalloc float[3];
            Native.Api.getPosition(handle, v);
            return new Vector3(v[0], v[1], v[2]);
        }
        set
        {
            float* v = stackalloc float[3] { value.X, value.Y, value.Z };
            Native.Api.setPosition(handle, v);
        }
    }

    public Quaternion Rotation
    {
        get
        {
            float* v = stackalloc float[4];
            Native.Api.getRotation(handle, v);
            return new Quaternion(v[0], v[1], v[2], v[3]);
        }
        set
        {
            float* v = stackalloc float[4] { value.X, value.Y, value.Z, value.W };
            Native.Api.setRotation(handle, v);
        }
    }

    public Vector3 Scale
    {
        get
        {
            float* v = stackalloc float[3];
            Native.Api.getScale(handle, v);
            return new Vector3(v[0], v[1], v[2]);
        }
        set
        {
            float* v = stackalloc float[3] { value.X, value.Y, value.Z };
            Native.Api.setScale(handle, v);
        }
    }
}

}
