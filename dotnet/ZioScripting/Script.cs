namespace Zio
{

public abstract class Script
{
    public Node Node { get; internal set; }

    public virtual void OnStart()
    {
    }

    public virtual void OnUpdate(float dt)
    {
    }

    public virtual void OnDestroy()
    {
    }
}

}
