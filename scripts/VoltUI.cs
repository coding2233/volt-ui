using System.Runtime.CompilerServices;

internal static class VoltUI
{
    [MethodImpl(MethodImplOptions.InternalCall)]
    internal static extern void Text(string text);

    [MethodImpl(MethodImplOptions.InternalCall)]
    internal static extern bool Button(string label);

    [MethodImpl(MethodImplOptions.InternalCall)]
    internal static extern float GetDeltaTime();

    [MethodImpl(MethodImplOptions.InternalCall)]
    internal static extern long GetFrameCount();
}
