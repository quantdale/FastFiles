using System;
using System.Windows.Automation;

// Observes only the real owned window's public Name property. The managed
// callback has no PowerShell runspace dependency on UIA's delivery thread.
public sealed class FastFilesOperationTitleObserver : IDisposable {
    private readonly AutomationElement element;
    private readonly AutomationPropertyChangedEventHandler handler;
    private readonly object sync = new object();
    private long version;
    private long terminalVersion;
    private string terminalTitle = "";
    private string latestTitle = "";

    public FastFilesOperationTitleObserver(AutomationElement window) {
        element = window;
        handler = OnChanged;
        Automation.AddAutomationPropertyChangedEventHandler(
            element, TreeScope.Element, handler, AutomationElement.NameProperty);
    }

    private void OnChanged(object sender, AutomationPropertyChangedEventArgs args) {
        string title = args.NewValue as string;
        if (title == null) return;
        lock (sync) {
            ++version;
            latestTitle = title;
            if (title.StartsWith("FastFiles — complete —", StringComparison.Ordinal) ||
                title.StartsWith("FastFiles — complete with errors —", StringComparison.Ordinal) ||
                title.StartsWith("FastFiles — stopped with errors —", StringComparison.Ordinal) ||
                title.StartsWith("FastFiles — cancelled —", StringComparison.Ordinal)) {
                terminalVersion = version;
                terminalTitle = title;
            }
        }
    }

    public long Version { get { lock (sync) { return version; } } }
    public long TerminalVersion { get { lock (sync) { return terminalVersion; } } }
    public string LatestTitle { get { lock (sync) { return latestTitle; } } }

    public string TerminalAfter(long observedVersion) {
        lock (sync) {
            return terminalVersion > observedVersion ? terminalTitle : null;
        }
    }

    public void Dispose() {
        Automation.RemoveAutomationPropertyChangedEventHandler(element, handler);
    }
}
