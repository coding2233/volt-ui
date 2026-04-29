using System;

internal static class AppMain
{
    private static bool _showDemo = false;

    public static void OnCreate()
    {
        Console.WriteLine("[C#] Script loaded!");
    }

    public static void OnUpdate(float dt)
    {
        if (VoltUI.Button("Hello from C#"))
        {
            VoltUI.Text("Button was clicked!");
        }

        if (VoltUI.Button("Toggle Demo"))
        {
            _showDemo = !_showDemo;
        }

        VoltUI.Text($"DT: {dt * 1000:F2} ms  Frame: {VoltUI.GetFrameCount()}");
    }

    public static void OnDestroy()
    {
        Console.WriteLine("[C#] Script stopped!");
    }
}
