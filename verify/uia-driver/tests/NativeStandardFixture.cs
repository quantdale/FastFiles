using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;

// Explicit diagnostic only: raw standard controls on an owned STA thread.
// No custom accessibility provider, injected event, or application input.
public static class FfStandardFixture {
    [UnmanagedFunctionPointer(CallingConvention.Winapi)]
    private delegate IntPtr WindowProc(IntPtr hwnd, uint message, IntPtr wparam, IntPtr lparam);

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    private struct WindowClass {
        public uint style;
        public WindowProc proc;
        public int classExtra, windowExtra;
        public IntPtr instance, icon, cursor, background;
        public string menu, name;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct Message {
        public IntPtr hwnd;
        public uint message;
        public UIntPtr wparam;
        public IntPtr lparam;
        public uint time;
        public int x, y;
        public uint privateData;
    }

    [DllImport("user32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern ushort RegisterClassW(ref WindowClass windowClass);
    [DllImport("user32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern IntPtr CreateWindowExW(uint extendedStyle, string className, string title,
        uint style, int x, int y, int width, int height, IntPtr parent, IntPtr id, IntPtr instance, IntPtr data);
    [DllImport("user32.dll")]
    private static extern IntPtr DefWindowProcW(IntPtr hwnd, uint message, IntPtr wparam, IntPtr lparam);
    [DllImport("user32.dll")]
    private static extern int GetMessageW(out Message message, IntPtr hwnd, uint first, uint last);
    [DllImport("user32.dll")]
    private static extern bool TranslateMessage(ref Message message);
    [DllImport("user32.dll")]
    private static extern IntPtr DispatchMessageW(ref Message message);
    [DllImport("user32.dll")]
    private static extern void PostQuitMessage(int code);
    [DllImport("user32.dll")]
    private static extern bool DestroyWindow(IntPtr hwnd);
    [DllImport("user32.dll")]
    private static extern bool ShowWindow(IntPtr hwnd, int command);
    [DllImport("user32.dll")]
    private static extern IntPtr SetCapture(IntPtr hwnd);
    [DllImport("user32.dll")]
    private static extern bool ReleaseCapture();
    [DllImport("user32.dll")]
    public static extern IntPtr SendMessageW(IntPtr hwnd, uint message, IntPtr wparam, IntPtr lparam);
    [DllImport("user32.dll")]
    private static extern bool PostMessageW(IntPtr hwnd, uint message, IntPtr wparam, IntPtr lparam);
    [DllImport("user32.dll")]
    public static extern bool IsWindow(IntPtr hwnd);
    [DllImport("user32.dll")]
    public static extern uint GetWindowThreadProcessId(IntPtr hwnd, out uint processId);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    private static extern int GetClassNameW(IntPtr hwnd, StringBuilder text, int count);

    private static readonly WindowProc callback = OnMessage;
    private static readonly ManualResetEvent ready = new ManualResetEvent(false);
    private static Thread thread;
    public static IntPtr Window, Check, Combo, FreshCheck, FreshCombo;
    public static string Error;

    private static IntPtr CreateControl(string className, string title, int y, int id) {
        // Both BS_AUTOCHECKBOX and CBS_DROPDOWNLIST have low style value 3.
        var hwnd = CreateWindowExW(0, className, title, 0x50010003, 20, y, 240,
            className == "BUTTON" ? 30 : 100, Window, (IntPtr)id, IntPtr.Zero, IntPtr.Zero);
        if (hwnd == IntPtr.Zero) throw new Win32Exception(Marshal.GetLastWin32Error());
        return hwnd;
    }

    private static void CreateControls(bool fresh) {
        var check = CreateControl("BUTTON", fresh ? "Fresh checkbox" : "Probe checkbox", fresh ? 120 : 20, fresh ? 103 : 101);
        var combo = CreateControl("COMBOBOX", "", fresh ? 160 : 60, fresh ? 104 : 102);
        if (fresh) { FreshCheck = check; FreshCombo = combo; }
        else { Check = check; Combo = combo; }
    }

    private static IntPtr OnMessage(IntPtr hwnd, uint message, IntPtr wparam, IntPtr lparam) {
        if (message == 0x8001) { CreateControls(true); return IntPtr.Zero; }
        if (message == 0x8002) { SetCapture(Check); return IntPtr.Zero; }
        if (message == 0x8003) { ReleaseCapture(); return IntPtr.Zero; }
        if (message == 0x10) { ReleaseCapture(); DestroyWindow(hwnd); return IntPtr.Zero; }
        if (message == 2) { PostQuitMessage(0); return IntPtr.Zero; }
        return DefWindowProcW(hwnd, message, wparam, lparam);
    }

    public static string ClassOf(IntPtr hwnd) {
        var text = new StringBuilder(256);
        GetClassNameW(hwnd, text, text.Capacity);
        return text.ToString();
    }

    public static void Start() {
        thread = new Thread(() => {
            try {
                var windowClass = new WindowClass { proc = callback, name = "FastFilesOwnedStandardUiaFixture" };
                if (RegisterClassW(ref windowClass) == 0) throw new Win32Exception(Marshal.GetLastWin32Error());
                Window = CreateWindowExW(0, windowClass.name, "Owned native UIA fixture", 0x00CF0000,
                    100, 100, 400, 300, IntPtr.Zero, IntPtr.Zero, IntPtr.Zero, IntPtr.Zero);
                if (Window == IntPtr.Zero) throw new Win32Exception(Marshal.GetLastWin32Error());
                CreateControls(false);
                ShowWindow(Window, 4); // SW_SHOWNOACTIVATE
                ready.Set();
                Message message;
                while (GetMessageW(out message, IntPtr.Zero, 0, 0) > 0) {
                    TranslateMessage(ref message);
                    DispatchMessageW(ref message);
                }
            } catch (Exception error) { Error = error.ToString(); ready.Set(); }
        });
        thread.SetApartmentState(ApartmentState.STA);
        thread.Start();
        if (!ready.WaitOne(5000)) throw new Exception("Owned fixture startup timed out.");
        if (Error != null) throw new Exception(Error);
    }

    public static bool Close() {
        if (Window != IntPtr.Zero) PostMessageW(Window, 0x10, IntPtr.Zero, IntPtr.Zero);
        return thread == null || thread.Join(5000);
    }
}
