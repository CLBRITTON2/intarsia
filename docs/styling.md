# Styling

The bar's appearance lives in `style.conf`, next to `config.lua`. Anything the format doesn't take is an error that
names the line, and saving reloads the bar.

## Format

```
// A comment runs from // to the end of the line.
bar:
  font: Segoe UI
  size: 13
  color: #e0def4

clock:
  padding: 0 8
  hover:
    background: #403d52
```

- Each line is blank, a comment, a block (`key:`) or a property (`key: value`).
- A block's lines are indented two spaces deeper than its key. Tabs, odd indents and lines indented more than one
  level past their block are errors. A block must hold at least one line. Blocks nest at most 8 levels deep.
- A key appears once per block.
- The file is at most 64 KiB, and a value is at most 1024 characters once its vars are filled in.
- Keys are letters, digits, `_` and `-`. A value runs to the end of the line or its comment.

## Values

- Numbers are DIPs with no unit, scaled with the monitor's DPI, from -10000 to 10000.
- `padding` and `margin` take one number (every side) or two (above and below, then left and right).
- Colors: `#rgb`, `#rgba`, `#rrggbb` or `#rrggbbaa`, optionally followed by an opacity that multiplies the alpha
  (`#191724 90%`), or `transparent`. Named colors aren't supported.
- Variables: define them in the `vars` block and use them as `$name` anywhere in a value. A var can't use another var.

```
vars:
  accent: #60a5fa
  box: #1c1c1e 92%

module:
  icon:
    color: $accent
```

## Blocks

The top level holds:

| Block | Styles |
| --- | --- |
| `vars` | Variables. |
| `bar` | The bar-wide `font`, `size` and `color`. Required, with all three. |
| `section` | The box around each of the left, center and right sections. |
| `module` | Every module. |
| `<name>` | One module, by its name in the config, over `module`. |
| `tooltip` | The hover tooltip. |
| `menu` | The popup menu, with `item` and `scrollbar` blocks inside. |

There are no blocks for a module type: a second clock is styled under its own name. A block naming no module in the
config is an error, and so is a module named `vars`, `bar`, `section`, `module`, `tooltip` or `menu`.

Inside a module (`module` or `<name>`):

| Block | Applies to |
| --- | --- |
| `icon` | The module's icon. |
| `item` | One element of a multi-element module: a workspace, a window icon, a binding mode, a tray icon. |
| `hover` | The module under the cursor. |
| `alert` | A module reporting a failure, such as a custom command that failed. |
| `warning`, `critical` | A battery running low, and nearly empty. Battery modules only. |
| `muted` | A muted audio module. Audio modules only. |
| `expanded` | An unfolded tray or group. Tray and group modules only. |
| a class | A custom module's own class, see [Custom modules](custom-modules.md#classes). |

Inside `item`: `hover`, and the states `focused`, `occupied` and `empty` (workspace and window items) and `toggle` (the
tray's toggle). Inside `menu > item`: `hover` and `active` (a row marked active), and `active` takes its own `hover`.

`module` styles every module, so it can't hold what only some draw: `warning`, `critical`, `muted`, `expanded`, an
icon's `rotate` and `transition`, or a `width` transition. Those go in the module's own block. A class goes in `module`
only when some custom module lists it.

## Properties per block

Box properties are `background`, `border` (and `border-top`, `-right`, `-bottom`, `-left`), `radius`, `padding` and
`shadow`.

| Block | Takes |
| --- | --- |
| `bar` | `font`, `size`, `color` |
| `section` | box, `margin`, `blur`, `brightness`, `saturate` |
| module | box, `margin`, `color`, `transition` |
| module `hover` | `background` |
| module `alert` | `color` |
| module `warning`, `critical`, `muted`, a class | `color`, and an `icon` block with `color` |
| `icon` | `font`, `size`, `color`, `margin-right`, `rotate`, `transition` |
| `expanded` | an `icon` block with `rotate` |
| `item` and its states | `color`, `background`, `weight`, `padding`, `margin`, `radius`, `shadow`, `transition` |
| `item > hover` | `color`, `background` |
| `tooltip` | box, `color`, `blur`, `brightness`, `saturate` |
| `menu` | box, `color`, `blur`, `brightness`, `saturate`, `max-width`, `max-height` |
| `menu > item`, `menu > item > active` | as `item`, without `transition` |
| `menu > item > hover` | `color`, `background` |
| `menu > scrollbar` | `width`, `background`, `radius`, `margin`. No scrollbar draws without a `width`. |

Hover blocks change colors only, so nothing moves under the cursor.

## Property notes

- `font`: one installed family, unquoted: `font: Segoe MDL2 Assets`.
- `weight`: `normal`, `bold`, or 100 to 900 in steps of 100.
- `border`: `none`, or a width and a color: `border-left: 1 #ffffff 20%`. A border sits inside the box.
- `shadow`: `none`, or comma separated shadows drawn inside the box, the first on top. Each is an x and y offset, an
  optional blur and spread, and a color: `shadow: 0 1 0 #ffffff 15%, 0 -1 1 #000000 25%`. An item's shadows fade over
  its `background` transition.
- `blur`: `on` or `off`. The system's blur of what lies behind the box, which shows through a translucent
  `background`. `brightness` and `saturate` tone it (numbers of 0 or more, 1 unchanged) and need `blur: on` beside
  them.
- `rotate`: degrees, for turning the tray or group toggle when it unfolds.
- `transition`: `none`, or comma separated `property duration timing`, with the duration in `ms` or `s` up to 60 s
  and timing one of `linear`, `ease`, `ease-in`, `ease-out`, `ease-in-out` (`ease` when left out). A module animates
  `background`, and a tray or group also `width` as it unfolds. An icon animates `rotate`, and items animate
  `background` and `color`.
- `max-width`, `max-height`: menu only. A taller menu scrolls.

## Precedence

A property comes from the most specific block that sets it: a module's own block over `module`, and a state over its
item or module. Under the cursor, an item state's colors win over `item > hover`, so a focused workspace keeps its
look. A side's `border-left` wins over `border` in the same block. `color`, `font` and `size` that no block sets
inherit from `bar` to the module to its icon and items. A state's text color reaches its icon only when no `icon`
block sets a color.

## Example

```
vars:
  text: #f2f2f2
  accent: #60a5fa
  box: #1c1c1e 92%
  line: #ffffff 8%
  hover: #ffffff 10%
  red: #f87171
  yellow: #facc15

bar:
  font: Segoe UI
  size: 13
  color: $text

section:
  margin: 6
  padding: 3
  background: $box
  border: 1 $line
  radius: 10
  blur: on

tooltip:
  padding: 8
  background: $box
  border: 1 $line
  radius: 8

module:
  padding: 0 10
  radius: 7
  transition: background 150ms ease
  hover:
    background: $hover
  icon:
    font: Segoe MDL2 Assets
    size: 14
    margin-right: 7
    color: $accent
  alert:
    color: $red
  item:
    padding: 4
    radius: 5
    transition: background 150ms ease, color 150ms ease
    hover:
      background: $hover

battery:
  warning:
    icon:
      color: $yellow
  critical:
    color: $red
    icon:
      color: $red

tray:
  transition: width 300ms ease-out
  icon:
    transition: rotate 250ms ease
  expanded:
    icon:
      rotate: 180

workspaces:
  item:
    focused:
      weight: bold
      shadow: 0 -2 0 $accent
```

`config\glazewm\style.conf` styles every module that config shows, with comments.
