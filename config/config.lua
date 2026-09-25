-- intarsia reads this from %USERPROFILE%\.config\intarsia\config.lua. Every key is required.
return {
    bar = {
        edge = "top", -- top, bottom, left or right
        -- top: above other windows, lowered while a fullscreen app runs. bottom: other windows can cover it.
        layer = "top",
        thickness = 54, -- DIPs, scaled by each monitor's DPI
        border = 1,
        spacing = 8, -- between modules and at both ends of the bar
    },
    font = {
        -- Propo keeps icon glyphs at their natural width, the plain Nerd Font squeezes them into one cell.
        family = "JetBrainsMono Nerd Font Propo",
        text_size = 15,
        icon_size = 18,
    },
    colors = {
        background = "#191724e6",
        border = "#6e6a8640",
        foreground = "#e0def4",
        accent = "#c4a7e7",
        alert = "#eb6f92", -- needs attention, such as a disconnected window manager
    },

    -- glazewm: workspaces and focus over GlazeWM's IPC, retried while GlazeWM is down
    -- windows: no window manager, windows and focus come from Windows itself (no workspaces or windows module)
    window_manager = "glazewm",

    -- The lists show modules by the names defined in modules below, left to right. A name in a list that modules
    -- does not define, or a module no list shows, is an error.
    modules_left = { "workspaces", "apps", "title" },
    modules_center = { "date" },
    modules_right = { "cpu", "memory" },

    -- Each module has a name you choose and a type: clock, window, workspaces, windows, cpu, memory or custom. One
    -- type can appear under several names with different settings, such as a second clock with its own format.
    --
    -- clock, window, cpu, memory and custom take optional on_click, on_right_click, on_scroll_up and on_scroll_down
    -- commands: { program, arguments... }. The program must be an .exe (absolute, or found on PATH when the config
    -- loads), and it starts without a shell, so arguments reach it exactly as written.
    --
    -- They also take an optional tooltip: a second format for the same value, shown while the cursor rests on the
    -- module. The workspaces module shows why the window manager is disconnected, with no key needed.
    modules = {
        workspaces = { type = "workspaces" },
        -- An icon per window on the displayed workspace, at font.icon_size. Click one to focus it.
        apps = { type = "windows" },
        title = {
            type = "window",
            max_length = 80, -- longer titles are cut with an ellipsis
            tooltip = "{}", -- the whole title
        },
        date = {
            type = "clock",
            format = "{:%a %d %b %Y, %H:%M}", -- std::format chrono spec
            tooltip = "{:%A %d %B %Y, %H:%M:%S}",
            icon = "\u{F1441}",
            interval = 60, -- seconds between redraws, aligned to the clock
        },
        cpu = {
            type = "cpu",
            format = "{}%", -- the value is an integer percent
            icon = "\u{F4BC}",
            interval = 1,
            on_click = { "C:\\Windows\\System32\\Taskmgr.exe" },
        },
        memory = {
            type = "memory",
            format = "{:.1f} GB", -- the value is gibibytes in use, as a double
            icon = "\u{EFC5}",
            interval = 1,
            on_click = { "C:\\Windows\\System32\\resmon.exe" },
        },
        -- A custom module runs its command every interval and shows what it printed. The command follows the same
        -- rules as on_click, runs with no window, and is killed with everything it started after timeout seconds.
        -- output = "text" shows the first line. output = "json" reads one object, { "text": "...", "tooltip": "...",
        -- "alert": true }, where only text is required, tooltip may span lines, and alert draws the text in the alert
        -- color. Control characters (other than line breaks in a tooltip) fail the run, as does a failed command, and
        -- either shows its reason in the alert color. Add "weather" to a list to show it.
        -- weather = {
        --     type = "custom",
        --     -- Absolute, so another curl.exe earlier on PATH (such as MSYS2's) is never picked instead.
        --     command = { "C:\\Windows\\System32\\curl.exe", "--silent", "--fail", "https://wttr.in/?format=%c%t" },
        --     timeout = 10,
        --     output = "text",
        --     format = "{}", -- the value is the output's text, as a string
        --     icon = "",
        --     interval = 900,
        -- },
    },
}
