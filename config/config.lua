-- intarsia reads this from %USERPROFILE%\.config\intarsia\config.lua. Every key is required.
return {
    bar = {
        edge = "top", -- top, bottom, left or right
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
    -- windows: no window manager, windows and focus come from Windows itself (no workspaces module)
    window_manager = "glazewm",

    modules_left = { "workspaces", "window" },
    modules_center = { "clock" },
    modules_right = { "cpu", "memory" },

    clock = {
        format = "{:%a %d %b %Y, %H:%M}", -- std::format chrono spec
        icon = "\u{F1441}",
        interval = 60, -- seconds between redraws, aligned to the clock
    },
    window = {
        max_length = 80, -- longer titles are cut with an ellipsis
    },
    -- Any text module takes optional on_click, on_right_click, on_scroll_up and on_scroll_down commands:
    -- { program, arguments... }. The program must be an .exe (absolute, or found on PATH when the config loads),
    -- and it starts without a shell, so arguments reach it exactly as written.
    cpu = {
        format = "{}%", -- the value is an integer percent
        icon = "\u{F4BC}",
        interval = 1,
        on_click = { "C:\\Windows\\System32\\Taskmgr.exe" },
    },
    memory = {
        format = "{:.1f} GB", -- the value is gibibytes in use, as a double
        icon = "\u{EFC5}",
        interval = 1,
        on_click = { "C:\\Windows\\System32\\resmon.exe" },
    },
}
