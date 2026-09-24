-- intarsia reads this from %USERPROFILE%\.config\intarsia\config.lua. Every key is required.
return {
    bar = {
        edge = "top", -- top, bottom, left or right
        thickness = 54, -- DIPs, scaled by each monitor's DPI
        border = 1,
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
    },
}
