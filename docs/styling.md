# Styling

The bar's appearance lives in `style.css`, next to `config.lua`. It's a strict subset of CSS. Anything unsupported is
an error that names the line, and saving reloads the bar.

## Values

- Colors: `#rgb`, `#rrggbb`, `#rrggbbaa`, `rgb(r, g, b)`, `rgba(r, g, b, a)` or `transparent`. Named colors aren't
  supported.
- Lengths: `px`, in DIPs, scaled with the monitor's DPI. A bare `0` works too.
- `padding` and `margin` take one value (all sides) or two (vertical, horizontal).
- Variables: define them in `:root` and use them with `var()`.

```css
:root {
    --accent: #60a5fa;
}

.icon {
    color: var(--accent);
}
```

## Selectors

| Selector | Targets |
| --- | --- |
| `*` | The whole bar: default `font-family`, `font-size` and `color`. |
| `section` | The box around each of the left, center and right sections. |
| `.module`, `clock`, `#clock` | Every module, every module of a type, or one module by its config name. |
| `.icon`, `#cpu .icon` | A module's icon. |
| `.item`, `workspaces .item` | One element of a multi-element module: a workspace, a window icon, a tray icon. |
| `tooltip` | The hover tooltip. |
| `menu`, `menu .item`, `menu .scrollbar` | The popup menu, its rows and its scrollbar. |

States:

| State | Applies to |
| --- | --- |
| `:hover` | Modules, items and menu rows. |
| `.focused`, `.occupied`, `.empty` | Workspace and window items. |
| `.toggle` | The tray's toggle item. |
| `.active` | A menu row marked active. |
| `.expanded` | An unfolded tray or group, before `.icon`: `tray.expanded .icon`. |
| `.alert` | A module reporting a failure, such as a custom command that failed. |
| `.warning`, `.critical` | A low battery. |
| `.muted` | A muted audio module. |

A custom module's own classes work the same way (`#weather.rain`), see [Custom modules](custom-modules.md#classes).

A selector has at most two parts, joined by a space. No other combinators, no `@` rules, no nesting. A `#name` that
isn't a module in the config is an error.

## Properties per selector

Box properties are `background`, `border` (and `border-top`, `-right`, `-bottom`, `-left`, `-width`, `-color`),
`border-radius`, `padding` and `box-shadow`.

| Selector | Takes |
| --- | --- |
| `*` | `font-family`, `font-size`, `color` |
| `section` | box, `margin`, `backdrop-filter` |
| module (`.module`, `clock`, `#clock`) | box, `margin`, `color`, `transition` |
| module `:hover` | `background` |
| module `.alert`, `.warning`, `.critical`, `.muted`, custom class | `color` |
| `.icon` | `font-family`, `font-size`, `color`, `margin-right`, `transform`, `transition` |
| state `.icon` (`battery.critical .icon`) | `color` |
| `.expanded .icon` | `transform` |
| `.item` | `color`, `background`, `font-weight`, `padding`, `margin`, `border-radius`, `box-shadow`, `transition` |
| `.item:hover` | `color`, `background` |
| `tooltip` | box, `color`, `backdrop-filter` |
| `menu` | box, `color`, `backdrop-filter`, `max-width`, `max-height` |
| `menu .item` | as `.item`, without `transition` |
| `menu .item:hover` | `color`, `background` |
| `menu .scrollbar` | `width`, `background`, `border-radius`, `margin` |

Hover and state rules only change colors, so nothing resizes under the cursor.

## Property notes

- `background-color` is an alias for `background`.
- `font-weight`: `normal`, `bold`, or 100 to 900 in steps of 100.
- `border`: width, `solid` and a color in any order, or `none`. `solid` is the only style.
- `box-shadow`: inset shadows only, comma separated. Each is `inset`, x and y offsets, optional blur and spread, and a
  color: `inset 0 1px 0 rgba(255, 255, 255, 0.15)`.
- `backdrop-filter`: `blur`, optionally followed by `brightness()` and `saturate()`, or `none`. The blur radius is the
  system's, so `blur` takes no value: `blur brightness(0.8) saturate(140%)`. It shows through a translucent
  background.
- `transform`: `rotate(<n>deg)` or `none`. Mainly for flipping the tray or group toggle when it unfolds.
- `transition`: comma separated `property duration timing`, with timing one of `linear`, `ease`, `ease-in`, `ease-out`,
  `ease-in-out` (default `ease`). Modules animate `background`, plus `width` on a tray or group unfolding. Icons
  animate `transform`, and items animate `background` and `color`.
- `max-width`, `max-height`: menu only. A taller menu scrolls.
- Lengths are capped at 10000px and durations at 60s. `!important` isn't supported.

## Precedence

A more specific selector wins (`#clock` over `clock` over `.module`). Between equally specific rules, the later one
wins.

## Example

```css
:root {
    --text: #f2f2f2;
    --accent: #60a5fa;
    --box: rgba(28, 28, 30, 0.92);
    --line: rgba(255, 255, 255, 0.08);
    --hover: rgba(255, 255, 255, 0.1);
    --red: #f87171;
    --yellow: #facc15;
}

* {
    font-family: "Segoe UI";
    font-size: 13px;
    color: var(--text);
}

section {
    margin: 6px;
    padding: 3px;
    background: var(--box);
    border: 1px solid var(--line);
    border-radius: 10px;
    backdrop-filter: blur;
}

tooltip {
    padding: 8px;
    background: var(--box);
    border: 1px solid var(--line);
    border-radius: 8px;
}

.module {
    padding: 0 10px;
    border-radius: 7px;
    transition: background 150ms ease;
}

.module:hover {
    background: var(--hover);
}

.icon {
    font-family: "Segoe MDL2 Assets";
    font-size: 14px;
    margin-right: 7px;
    color: var(--accent);
}

.alert {
    color: var(--red);
}

battery.warning .icon {
    color: var(--yellow);
}

battery.critical, battery.critical .icon {
    color: var(--red);
}

tray {
    transition: width 300ms ease-out;
}

tray .icon {
    transition: transform 250ms ease;
}

tray.expanded .icon {
    transform: rotate(180deg);
}

.item {
    padding: 4px;
    border-radius: 5px;
    transition: background 150ms ease, color 150ms ease;
}

.item:hover {
    background: var(--hover);
}

workspaces .item.focused {
    font-weight: bold;
    box-shadow: inset 0 -2px 0 var(--accent);
}
```

`config\glazewm\style.css` uses every selector, with comments.
