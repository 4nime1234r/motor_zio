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

    internal IntPtr Handle => handle;

    public bool IsValid => handle != IntPtr.Zero;

    public override bool Equals(object other)
    {
        return other is Node node && node.handle == handle;
    }

    public override int GetHashCode()
    {
        return handle.GetHashCode();
    }

    private static Node Wrap(IntPtr handle)
    {
        return handle == IntPtr.Zero ? null : new Node(handle);
    }

    public static Node Create(Node parent, NodeKind kind = NodeKind.Node, string name = "")
    {
        if (parent == null || !parent.IsValid)
        {
            return null;
        }
        return Wrap(Native.WithUtf8(name, (p, n) => Native.Api.createNode(parent.handle, (int)kind, (byte*)p, n)));
    }

    public static Node Instantiate(Node parent, string prefabPath)
    {
        if (parent == null || !parent.IsValid)
        {
            return null;
        }
        return Wrap(Native.WithUtf8(prefabPath, (p, n) => Native.Api.instantiate(parent.handle, (byte*)p, n)));
    }

    public void Destroy()
    {
        if (handle != IntPtr.Zero)
        {
            Native.Api.destroyNode(handle);
        }
    }

    public Node Parent => handle == IntPtr.Zero ? null : Wrap(Native.Api.getParent(handle));

    public int ChildCount => handle == IntPtr.Zero ? 0 : Native.Api.getChildCount(handle);

    public Node GetChild(int index)
    {
        return handle == IntPtr.Zero ? null : Wrap(Native.Api.getChild(handle, index));
    }

    public Node FindChild(string name)
    {
        if (handle == IntPtr.Zero)
        {
            return null;
        }
        return Wrap(Native.WithUtf8(name, (p, n) => Native.Api.findChild(handle, (byte*)p, n)));
    }

    public bool Visible
    {
        get => handle != IntPtr.Zero && Native.Api.isVisible(handle) != 0;
        set
        {
            if (handle != IntPtr.Zero)
            {
                Native.Api.setVisible(handle, value ? 1 : 0);
            }
        }
    }

    public bool SetMesh(string source)
    {
        return handle != IntPtr.Zero && Native.WithUtf8(source, (p, n) => Native.Api.setMesh(handle, (byte*)p, n)) != 0;
    }

    public void SetTexture(string path)
    {
        if (handle != IntPtr.Zero)
        {
            Native.WithUtf8(path, (p, n) =>
            {
                Native.Api.setTexture(handle, (byte*)p, n);
                return 0;
            });
        }
    }

    public void SetColor(Vector4 color)
    {
        if (handle != IntPtr.Zero)
        {
            float* v = stackalloc float[4] { color.X, color.Y, color.Z, color.W };
            Native.Api.setColor(handle, v);
        }
    }

    public void SetLight(float intensity, float range)
    {
        if (handle != IntPtr.Zero)
        {
            Native.Api.setLight(handle, intensity, range);
        }
    }

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
        set
        {
            if (handle != IntPtr.Zero)
            {
                Native.WithUtf8(value, (p, n) =>
                {
                    Native.Api.setName(handle, (byte*)p, n);
                    return 0;
                });
            }
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
