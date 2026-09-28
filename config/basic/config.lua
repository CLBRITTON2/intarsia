-- A plain bar that needs nothing but Windows: no window manager, and icons from Segoe MDL2 Assets, which Windows ships.
-- Copy this file and style.css beside it to %USERPROFILE%\.config\intarsia\. config/example documents every key and
-- module.
return {
    bar = {
        edge = "top",
        layer = "top",
        space = "reserve",
        thickness = 44,
    },

    window_manager = "windows",

    modules_left = { "title" },
    modules_center = { "clock" },
    modules_right = { "tray", "cpu", "memory", "disk", "volume", "battery" },

    layouts = {},

    modules = {
        title = { type = "window", max_length = 80, tooltip = "{title}" },
        clock = {
            type = "clock",
            format = "{time:%a %d %b  %H:%M}",
            tooltip = "{time:%A %d %B %Y}",
            icon = "\u{E823}",
            interval = 60,
        },
        tray = { type = "tray", icon = "\u{E76C}" },
        cpu = { type = "cpu", format = "{usage}%", icon = "\u{E950}", interval = 2 },
        memory = { type = "memory", format = "{used:.1f} GB", icon = "\u{E964}", interval = 2 },
        disk = {
            type = "counter",
            counter = "\\PhysicalDisk(_Total)\\Disk Bytes/sec",
            combine = "sum",
            format = "{rate}",
            icon = "\u{EDA2}",
            interval = 2,
        },
        volume = {
            type = "audio",
            format = "{volume}%",
            format_muted = "muted",
            icon = "\u{E767}",
            icon_muted = "\u{E74F}",
            step = 5,
            tooltip = "{device}",
        },
        battery = {
            type = "battery",
            format_discharging = "{capacity}%",
            format_charging = "{capacity}%",
            format_plugged = "{capacity}%",
            icons = { "\u{E850}", "\u{E853}", "\u{E856}", "\u{E859}", "\u{E83F}" },
            icon_charging = "\u{E83E}",
            icon_plugged = "\u{EA93}",
            warning_at = 30,
            critical_at = 15,
            tooltip = "{capacity}% {time}",
        },
    },
}
