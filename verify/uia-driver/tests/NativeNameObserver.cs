using System;
using System.Runtime.InteropServices;
using System.Windows.Automation;

// A real UIA callback, deliberately outside PowerShell's UIA callback thread.
public static class FfNativeNameObserver {
    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    private static extern bool SetWindowTextW(IntPtr hwnd, string text);
    [DllImport("kernel32.dll")]
    private static extern void Sleep(uint milliseconds);
    private static volatile int count;
    private static volatile string latest;
    private static void Changed(object sender, AutomationPropertyChangedEventArgs args) {
        latest = args.Property.Id + ":" + Convert.ToString(args.NewValue);
        count++;
    }
    public static string Observe(IntPtr hwnd, string title) {
        var element = AutomationElement.FromHandle(hwnd);
        var handler = new AutomationPropertyChangedEventHandler(Changed);
        count = 0;
        latest = "";
        Automation.AddAutomationPropertyChangedEventHandler(element, TreeScope.Element, handler, AutomationElement.NameProperty);
        try {
            if (!SetWindowTextW(hwnd, title)) throw new Exception("Own fixture SetWindowText failed.");
            Sleep(750);
            return "count=" + count + ";latest=" + latest + ";current=" + element.Current.Name;
        } finally { Automation.RemoveAutomationPropertyChangedEventHandler(element, handler); }
    }
}
