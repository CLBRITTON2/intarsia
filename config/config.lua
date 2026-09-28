-- intarsia reads this from %USERPROFILE%\.config\intarsia\config.lua. Every key is required. How the bar looks lives in
-- style.css beside it.
return {
    bar = {
        edge = "top", -- top, bottom, left or right
        -- top: above other windows, lowered while a fullscreen app runs. bottom: other windows can cover it.
        layer = "top",
        thickness = 54, -- DIPs, scaled by each monitor's DPI
    },

    -- glazewm: workspaces and focus over GlazeWM's IPC, retried while GlazeWM is down
    -- windows: no window manager, windows and focus come from Windows itself (no workspaces, windows, binding_mode or
    -- tiling_direction module)
    window_manager = "glazewm",

    -- The lists show modules by the names defined in modules below, left to right. A name in a list that modules
    -- does not define, or a module no list shows, is an error. style.css styles a module by its name, as #date.
    modules_left = { "workspaces", "divider", "apps", "divider_date", "date" },
    modules_center = {},
    modules_right = { "tray", "mode", "tiling", "net", "cpu", "memory", "volume", "battery" },

    -- Each module has a name you choose and a type: clock, window, workspaces, windows, binding_mode,
    -- tiling_direction, divider, cpu, memory, audio, network, battery, tray or custom. One type can appear under several names with different
    -- settings, such as a second clock with its own format.
    --
    -- clock, window, cpu, memory, audio, network, battery and custom take optional on_click, on_right_click, on_scroll_up and on_scroll_down
    -- actions. { program, arguments... } starts a program: it must be an .exe (absolute, or found on PATH when the
    -- config loads), and it starts without a shell, so arguments reach it exactly as written.
    -- { window_manager = "focus --next-workspace" } sends the window manager a command in its own syntax instead,
    -- without starting a process (glazewm only).
    --
    -- They also take an optional tooltip: a second format for the same value, shown while the cursor rests on the
    -- module. The workspaces module shows why the window manager is disconnected, with no key needed.
    modules = {
        -- One item per workspace on this monitor: .focused, .occupied (has windows) or .empty. Click one to focus it.
        workspaces = { type = "workspaces" },
        -- A line between modules, sized by width and height in style.css. Each place a line appears is its own module.
        divider = { type = "divider" },
        divider_date = { type = "divider" },
        -- One item per window on the displayed workspace, .focused for the focused one. Click one to focus it.
        apps = { type = "windows" },
        -- One item per active binding mode, nothing while none is active.
        mode = { type = "binding_mode" },
        -- The focused container's tiling direction, drawn as an icon. Click to flip it.
        tiling = { type = "tiling_direction", horizontal = "⇄", vertical = "⇵" },
        date = {
            type = "clock",
            format = "{time:%a %d %b %Y, %H:%M}", -- {time} takes a std::format chrono spec
            tooltip = "{time:%A %d %B %Y, %H:%M:%S}",
            icon = "\u{F1441}",
            interval = 60, -- seconds between redraws, aligned to the clock
        },
        cpu = {
            type = "cpu",
            format = "{usage}%", -- {usage} is a whole percent
            icon = "\u{F4BC}",
            interval = 1,
            on_click = { "C:\\Windows\\System32\\Taskmgr.exe" },
        },
        memory = {
            type = "memory",
            format = "{used:.1f} GB", -- {used} is gibibytes in use, as a double
            icon = "\u{EFC5}",
            interval = 1,
            on_click = { "C:\\Windows\\System32\\resmon.exe" },
        },
        -- The interface the internet is reached through, sampled every second. Formats and the tooltip can use {ssid},
        -- {signal} (a percent), {ip}, {interface}, {down} and {up} (rates such as "1.2 MB/s"), and a name can take a
        -- std::format spec, as {signal:>3}. An empty format shows the icon alone. format_ethernet covers every link that
        -- is not Wi-Fi. Windows 11 hides the network's name and signal from apps without location access
        -- (Settings > Privacy & security > Location), and {ssid} is then empty.
        net = {
            type = "network",
            format_wifi = "",
            format_ethernet = "",
            format_disconnected = "",
            -- Weakest signal to strongest, the range split evenly between them.
            icons_wifi = { "\u{F092F}", "\u{F091F}", "\u{F0922}", "\u{F0925}", "\u{F0928}" },
            icon_ethernet = "\u{F0200}",
            icon_disconnected = "\u{F092E}",
            tooltip = "{interface} {ssid} {signal}%\n{ip}\n↓ {down}  ↑ {up}",
            on_click = { "C:\\Windows\\explorer.exe", "ms-settings:network-status" },
        },
        -- The default output device's volume, following device switches. Click toggles mute, scroll changes the volume
        -- by step percent, unless on_click or on_scroll_up and on_scroll_down replace them. The formats and tooltip can
        -- use {volume} (a whole percent) and {device} (its name). Shows nothing while there is no output device.
        volume = {
            type = "audio",
            format = "{volume}%",
            format_muted = "muted",
            icon = "\u{F057E}",
            icon_muted = "\u{F0581}",
            step = 5,
            tooltip = "{device}",
        },
        -- The batteries taken together, sampled when Windows reports a change of charge or power source. The formats and
        -- tooltip can use {capacity} (a whole percent), {time} (hours and minutes left on battery, as "1:05", empty
        -- until Windows has an estimate) and {health} (full charge capacity against design capacity, a whole percent,
        -- empty when the battery does not report it). format_plugged is on external power and not charging: full, or
        -- held by a charge limit. Shows nothing on a machine with no battery.
        battery = {
            type = "battery",
            format_discharging = "{capacity}%",
            format_charging = "{capacity}%",
            format_plugged = "{capacity}%",
            -- Emptiest to fullest while discharging, the range split evenly between them.
            icons = { "\u{F008E}", "\u{F007C}", "\u{F007E}", "\u{F0081}", "\u{F0079}" },
            icon_charging = "\u{F0084}",
            icon_plugged = "\u{F06A5}",
            -- Discharging at or under this percent draws the text in the .alert color.
            alert_at = 20,
            tooltip = "{capacity}% {time}\nHealth {health}%",
            on_click = { "C:\\Windows\\explorer.exe", "ms-settings:batterysaver" },
        },
        -- The notification area: every app's tray icons in the order the apps added them, folded away behind a
        -- toggle showing icon until it is clicked. Click an icon for its app's window, right click it for its menu.
        -- How it unfolds and how the toggle turns is style.css's. While a tray module shows, the bar receives the
        -- icons in the taskbar's place and passes each on to it, so the taskbar still has them all once the bar exits.
        tray = { type = "tray", icon = "\u{F054}" },
        -- A window module shows the focused window's title. Add "title" to a list to show it.
        -- title = {
        --     type = "window",
        --     max_length = 80, -- longer titles are cut with an ellipsis
        --     tooltip = "{title}", -- the whole title
        -- },
        --
        -- A custom module runs its command every interval and shows what it printed. The command follows the same
        -- rules as on_click, runs with no window, and is killed with everything it started after timeout seconds.
        -- output = "text" shows the first line. output = "json" reads one object, { "text": "...", "tooltip": "...",
        -- "alert": true }, where only text is required, tooltip may span lines, and alert draws the text in the
        -- module's .alert color. Control characters (other than line breaks in a tooltip) fail the run, as does a
        -- failed command, and either shows its reason as an alert. Add "weather" to a list to show it.
        -- weather = {
        --     type = "custom",
        --     -- Absolute, so another curl.exe earlier on PATH (such as MSYS2's) is never picked instead.
        --     command = { "C:\\Windows\\System32\\curl.exe", "--silent", "--fail", "https://wttr.in/?format=%c%t" },
        --     timeout = 10,
        --     output = "text",
        --     format = "{text}", -- {text} is the output's text, as a string
        --     icon = "",
        --     interval = 900,
        -- },
    },
}
