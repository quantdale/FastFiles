<#
    zero-touch-autonomous-engineering (uia-driver): internal constants and driver
    defaults. Dot-sourced by UiaDriver.psm1 before the public function files.
#>

$script:UiaDriverDefaults = [pscustomobject]@{
    Provider        = 'managed'
    TimeoutMs       = 5000
    PollIntervalMs  = 50
    MaxTreeDepth    = 20
    MaxTreeWidth    = 100
    View            = 'Control'
}

$script:UiaKnownViews = @('Control', 'Raw')

# ControlType programmatic names (UIAutomationTypes strips the "ControlType." prefix).
$script:UiaControlTypeNames = @(
    'Button','Calendar','CheckBox','ComboBox','Custom','DataGrid','DataItem','Document',
    'Edit','Group','Header','HeaderItem','Hyperlink','Image','List','ListItem','Menu',
    'MenuBar','MenuItem','Pane','ProgressBar','RadioButton','ScrollBar','SemanticZoom',
    'Separator','Slider','Spinner','SplitButton','StatusBar','Tab','TabItem','Table',
    'Text','Thumb','TitleBar','ToolBar','ToolTip','Tree','TreeItem','Window','AppBar'
)

# Event names the driver can subscribe to, resolved to UIA AutomationEvent objects.
# Resolvers are scriptblocks so the types are only touched after Add-Type loads them.
$script:UiaEventIdResolver = @{
    Invoked           = { [System.Windows.Automation.InvokePatternIdentifiers]::InvokedEvent }
    SelectionChanged  = { [System.Windows.Automation.SelectionPatternIdentifiers]::InvalidatedEvent }
    WindowOpened      = { [System.Windows.Automation.WindowPatternIdentifiers]::WindowOpenedEvent }
    WindowClosed      = { [System.Windows.Automation.WindowPatternIdentifiers]::WindowClosedEvent }
    StructureChanged  = { [System.Windows.Automation.AutomationElementIdentifiers]::StructureChangedEvent }
    ElementAdded      = { [System.Windows.Automation.AutomationElementIdentifiers]::StructureChangedEvent }
    ElementRemoved    = { [System.Windows.Automation.AutomationElementIdentifiers]::StructureChangedEvent }
}

# Pattern names this driver dispatches on (live + mock).
$script:UiaPatternNames = @('Invoke','SelectionItem','Selection','Scroll','Value','Window','Drag')

# VK codes for SendInput keyboard fallback.
$script:UiaVkMap = @{
    ENTER = 0x0D; RETURN = 0x0D; ESC = 0x1B; ESCAPE = 0x1B; TAB = 0x09
    DOWN = 0x28; UP = 0x26; LEFT = 0x25; RIGHT = 0x27
    HOME = 0x24; END = 0x23; PAGEUP = 0x21; PAGEDOWN = 0x22
    DEL = 0x2E; DELETE = 0x2E; BACKSPACE = 0x08; SPACE = 0x20
    F1 = 0x70; F2 = 0x71; F3 = 0x72; F4 = 0x73; F5 = 0x74; F6 = 0x75
    F7 = 0x76; F8 = 0x77; F9 = 0x78; F10 = 0x79; F11 = 0x7A; F12 = 0x7B
    A = 0x41; B = 0x42; C = 0x43; D = 0x44; E = 0x45; F = 0x46; G = 0x47
    H = 0x48; I = 0x49; J = 0x4A; K = 0x4B; L = 0x4C; M = 0x4D; N = 0x4E
    O = 0x4F; P = 0x50; Q = 0x51; R = 0x52; S = 0x53; T = 0x54; U = 0x55
    V = 0x56; W = 0x57; X = 0x58; Y = 0x59; Z = 0x5A
}
