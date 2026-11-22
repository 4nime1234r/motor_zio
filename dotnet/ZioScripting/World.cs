using System;
using System.Numerics;

namespace Zio
{

public enum NodeKind
{
    Node = 0,
    Mesh = 1,
    Light = 2
}

public enum BodyKind
{
    Static = 0,
    Dynamic = 1,
    Kinematic = 2
}

public readonly struct RayHit
{
    public readonly Body Body;
    public readonly float Distance;
    public readonly Vector3 Point;
    public readonly Vector3 Normal;

    internal RayHit(Body body, float distance, Vector3 point, Vector3 normal)
    {
        Body = body;
        Distance = distance;
        Point = point;
        Normal = normal;
    }
}

public readonly struct Contact
{
    public readonly Body A;
    public readonly Body B;
    public readonly Vector3 Point;
    public readonly Vector3 Normal;
    public readonly float Penetration;

    internal Contact(Body a, Body b, Vector3 point, Vector3 normal, float penetration)
    {
        A = a;
        B = b;
        Point = point;
        Normal = normal;
        Penetration = penetration;
    }
}

public enum JointKind
{
    Distance = 0,
    Ball = 1,
    Hinge = 2,
    Fixed = 3
}

public readonly struct TriggerEvent
{
    public readonly Body Trigger;
    public readonly Body Other;
    public readonly bool Entered;

    internal TriggerEvent(Body trigger, Body other, bool entered)
    {
        Trigger = trigger;
        Other = other;
        Entered = entered;
    }
}

public readonly unsafe struct Joint
{
    public readonly uint Handle;

    internal Joint(uint handle)
    {
        Handle = handle;
    }

    public bool IsValid => Handle != 0;

    public static Joint Create(JointKind kind, Body a, Body b, Vector3 anchor, Vector3 axis = default, float length = -1f)
    {
        float* at = stackalloc float[3] { anchor.X, anchor.Y, anchor.Z };
        float* direction = stackalloc float[3] { axis.X, axis.Y, axis.Z };
        return new Joint(Native.Api.createJoint((int)kind, a.Handle, b.Handle, at, direction, length));
    }

    public void Destroy()
    {
        Native.Api.destroyJoint(Handle);
    }
}

public readonly unsafe struct Body : IEquatable<Body>
{
    public readonly uint Handle;

    internal Body(uint handle)
    {
        Handle = handle;
    }

    public bool IsValid => Handle != 0 && Native.Api.hasBody(Handle) != 0;

    public static Body CreateSphere(BodyKind kind, Vector3 position, float radius, float mass = 1f, float restitution = 0.2f, float friction = 0.5f)
    {
        float* shape = stackalloc float[3] { radius, 0f, 0f };
        float* at = stackalloc float[3] { position.X, position.Y, position.Z };
        return new Body(Native.Api.createBody((int)kind, 0, shape, at, mass, restitution, friction));
    }

    public static Body CreateBox(BodyKind kind, Vector3 position, Vector3 halfExtents, float mass = 1f, float restitution = 0.2f, float friction = 0.5f)
    {
        float* shape = stackalloc float[3] { halfExtents.X, halfExtents.Y, halfExtents.Z };
        float* at = stackalloc float[3] { position.X, position.Y, position.Z };
        return new Body(Native.Api.createBody((int)kind, 1, shape, at, mass, restitution, friction));
    }

    public void Destroy()
    {
        Native.Api.destroyBody(Handle);
    }

    public void SetFilter(uint layer, uint mask, bool trigger = false)
    {
        Native.Api.setBodyFilter(Handle, layer, mask, trigger ? 1 : 0);
    }

    private Vector3 GetVector(int what)
    {
        float* v = stackalloc float[3];
        Native.Api.getBodyVector(Handle, what, v);
        return new Vector3(v[0], v[1], v[2]);
    }

    private void SetVector(int what, Vector3 value)
    {
        float* v = stackalloc float[3] { value.X, value.Y, value.Z };
        Native.Api.setBodyVector(Handle, what, v);
    }

    public Vector3 Position
    {
        get => GetVector(0);
        set => SetVector(0, value);
    }

    public Vector3 LinearVelocity
    {
        get => GetVector(1);
        set => SetVector(1, value);
    }

    public Vector3 AngularVelocity
    {
        get => GetVector(3);
        set => SetVector(3, value);
    }

    public Quaternion Rotation
    {
        get
        {
            float* v = stackalloc float[4];
            Native.Api.getBodyVector(Handle, 2, v);
            return new Quaternion(v[0], v[1], v[2], v[3]);
        }
        set
        {
            float* v = stackalloc float[4] { value.X, value.Y, value.Z, value.W };
            Native.Api.setBodyVector(Handle, 2, v);
        }
    }

    private void Apply(int what, Vector3 value)
    {
        float* v = stackalloc float[3] { value.X, value.Y, value.Z };
        Native.Api.applyBody(Handle, what, v);
    }

    public void ApplyImpulse(Vector3 impulse)
    {
        Apply(0, impulse);
    }

    public void ApplyForce(Vector3 force)
    {
        Apply(1, force);
    }

    public void ApplyTorque(Vector3 torque)
    {
        Apply(2, torque);
    }

    public void FollowNode(Node node)
    {
        node.Position = Position;
        node.Rotation = Rotation;
    }

    public bool Equals(Body other)
    {
        return Handle == other.Handle;
    }

    public override bool Equals(object other)
    {
        return other is Body body && Equals(body);
    }

    public override int GetHashCode()
    {
        return (int)Handle;
    }
}

public static unsafe class Physics
{
    public static Vector3 Gravity
    {
        get
        {
            float* v = stackalloc float[3];
            Native.Api.getGravity(v);
            return new Vector3(v[0], v[1], v[2]);
        }
        set
        {
            float* v = stackalloc float[3] { value.X, value.Y, value.Z };
            Native.Api.setGravity(v);
        }
    }

    public static bool Raycast(Vector3 origin, Vector3 direction, float maxDistance, out RayHit hit)
    {
        float* o = stackalloc float[3] { origin.X, origin.Y, origin.Z };
        float* d = stackalloc float[3] { direction.X, direction.Y, direction.Z };
        float* result = stackalloc float[7];
        uint body = 0;
        if (Native.Api.raycast(o, d, maxDistance, result, &body) == 0)
        {
            hit = default;
            return false;
        }
        hit = new RayHit(new Body(body), result[0], new Vector3(result[1], result[2], result[3]), new Vector3(result[4], result[5], result[6]));
        return true;
    }

    public static int TriggerEventCount => Native.Api.triggerEventCount();

    public static TriggerEvent GetTriggerEvent(int index)
    {
        uint trigger = 0;
        uint other = 0;
        int entered = 0;
        if (Native.Api.getTriggerEvent(index, &trigger, &other, &entered) == 0)
        {
            return default;
        }
        return new TriggerEvent(new Body(trigger), new Body(other), entered != 0);
    }

    public static int ContactCount => Native.Api.contactCount();

    public static Contact GetContact(int index)
    {
        uint a = 0;
        uint b = 0;
        float* v = stackalloc float[7];
        if (Native.Api.getContact(index, &a, &b, v) == 0)
        {
            return default;
        }
        return new Contact(new Body(a), new Body(b), new Vector3(v[0], v[1], v[2]), new Vector3(v[3], v[4], v[5]), v[6]);
    }
}

public enum SoundCategory
{
    Effects = 0,
    Music = 1
}

public readonly unsafe struct Sound
{
    public readonly uint Handle;

    internal Sound(uint handle)
    {
        Handle = handle;
    }

    public bool IsPlaying => Handle != 0 && Native.Api.isSoundPlaying(Handle) != 0;

    public void Stop()
    {
        Native.Api.stopSound(Handle);
    }

    public void SetVolume(float volume)
    {
        Native.Api.setSound(Handle, 0, volume);
    }

    public void SetPitch(float pitch)
    {
        Native.Api.setSound(Handle, 1, pitch);
    }
}

public static unsafe class Audio
{
    public static Sound Play(string path, float volume = 1f, float pitch = 1f, bool loop = false, SoundCategory category = SoundCategory.Effects)
    {
        uint voice = Native.WithUtf8(path, (p, n) => Native.Api.playSound((byte*)p, n, volume, pitch, loop ? 1 : 0, (int)category));
        return new Sound(voice);
    }

    public static void SetCategoryVolume(SoundCategory category, float volume)
    {
        Native.Api.setCategoryVolume((int)category, volume);
    }
}

public static unsafe class Scenes
{
    public static void Load(string path)
    {
        Native.WithUtf8(path, (p, n) =>
        {
            Native.Api.loadScene((byte*)p, n);
            return 0;
        });
    }
}

}
