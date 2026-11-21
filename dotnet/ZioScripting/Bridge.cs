using System;
using System.Collections.Generic;
using System.IO;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Runtime.Loader;

namespace Zio
{

internal sealed class ScriptContext : AssemblyLoadContext
{
    public ScriptContext() : base(isCollectible: true)
    {
    }

    protected override Assembly Load(AssemblyName name)
    {
        if (name.Name == "ZioScripting")
        {
            return typeof(Script).Assembly;
        }
        return null;
    }
}

public static unsafe class Bridge
{
    private sealed class Instance
    {
        public Script Script;
        public bool Failed;
    }

    private static ScriptContext context;
    private static readonly Dictionary<string, Type> types = new Dictionary<string, Type>();
    private static readonly Dictionary<int, Instance> instances = new Dictionary<int, Instance>();
    private static int nextHandle = 1;

    [UnmanagedCallersOnly]
    public static int Initialize(IntPtr api)
    {
        Native.Api = *(NativeApi*)api;
        return 1;
    }

    [UnmanagedCallersOnly]
    public static int LoadScripts(IntPtr pathUtf16)
    {
        try
        {
            string path = Marshal.PtrToStringUni(pathUtf16);
            Unload();
            if (string.IsNullOrEmpty(path) || !File.Exists(path))
            {
                return -1;
            }

            byte[] bytes = File.ReadAllBytes(path);
            context = new ScriptContext();
            Assembly assembly = context.LoadFromStream(new MemoryStream(bytes));
            foreach (Type type in assembly.GetTypes())
            {
                if (!type.IsAbstract && typeof(Script).IsAssignableFrom(type) && type.GetConstructor(Type.EmptyTypes) != null)
                {
                    types[type.Name] = type;
                }
            }
            return types.Count;
        }
        catch (Exception e)
        {
            Log.Error("No se pudieron cargar los scripts de C#: " + e.Message);
            return -1;
        }
    }

    [UnmanagedCallersOnly]
    public static int ListScripts(IntPtr buffer, int capacity)
    {
        string joined = string.Join("\n", types.Keys);
        if (buffer != IntPtr.Zero && capacity > joined.Length)
        {
            char* target = (char*)buffer;
            for (int i = 0; i < joined.Length; ++i)
            {
                target[i] = joined[i];
            }
            target[joined.Length] = '\0';
        }
        return joined.Length;
    }

    [UnmanagedCallersOnly]
    public static int CreateScript(IntPtr nameUtf16)
    {
        try
        {
            string name = Marshal.PtrToStringUni(nameUtf16);
            if (name == null || !types.TryGetValue(name, out Type type))
            {
                return 0;
            }
            int handle = nextHandle++;
            instances[handle] = new Instance { Script = (Script)Activator.CreateInstance(type) };
            return handle;
        }
        catch (Exception e)
        {
            Log.Error("No se pudo crear el script de C#: " + Describe(e));
            return 0;
        }
    }

    [UnmanagedCallersOnly]
    public static void StartScript(int handle, IntPtr node)
    {
        Run(handle, "OnStart", script =>
        {
            script.Node = new Node(node);
            script.OnStart();
        });
    }

    [UnmanagedCallersOnly]
    public static void UpdateScript(int handle, float dt)
    {
        Run(handle, "OnUpdate", script => script.OnUpdate(dt));
    }

    [UnmanagedCallersOnly]
    public static void DestroyScript(int handle)
    {
        if (!instances.TryGetValue(handle, out Instance instance))
        {
            return;
        }
        instances.Remove(handle);
        try
        {
            instance.Script.OnDestroy();
        }
        catch (Exception e)
        {
            Log.Error("Error en OnDestroy de " + instance.Script.GetType().Name + ": " + Describe(e));
        }
    }

    private static void Run(int handle, string phase, Action<Script> action)
    {
        if (!instances.TryGetValue(handle, out Instance instance) || instance.Failed)
        {
            return;
        }
        try
        {
            action(instance.Script);
        }
        catch (Exception e)
        {
            instance.Failed = true;
            Log.Error("Error en " + phase + " de " + instance.Script.GetType().Name + " (el script se desactiva): " + Describe(e));
        }
    }

    private static string Describe(Exception e)
    {
        Exception inner = e is TargetInvocationException && e.InnerException != null ? e.InnerException : e;
        return inner.GetType().Name + ": " + inner.Message;
    }

    private static void Unload()
    {
        instances.Clear();
        types.Clear();
        if (context != null)
        {
            context.Unload();
            context = null;
        }
    }
}

}
