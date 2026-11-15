# Theming the flattened GUI

The flattened GUI (the applications started with `--disable-stylesheets`, or with `O3DE_DISABLE_STYLESHEETS` set) draws every
control from values in code instead of Qt style sheets. Those values are *tokens*, and a *theme* is a file that gives tokens
other values while the application runs. The legacy GUI (style sheets) is not themed.

The first part of this note is for someone who uses or makes a theme; the second for a contributor who styles a widget or adds a
value a theme should reach.

## Using themes

Open **Tools > Theme Editor** in the Editor (File > Theme Editor in the control Gallery). Every change shows at once, in every
window of the application, and is saved as it is made.

- **Theme** picks the theme in effect. The engine ships *O3DE Original* (the values in the code: the look the style sheets gave)
  and *Modern Blue Steel*. The choice is kept for every O3DE application.
- **Changing a value of an engine theme makes a theme of your own** that is based on it: it holds only what you changed, and
  follows the engine's theme for the rest. Your themes are files in `~/.o3de/Themes`.
- **Recolor...** carries the whole theme over to another background and accent colour: the quick way to a scheme of your own.
  Started from O3DE Original it keeps the light entry fields, from Modern Blue Steel the dark ones.
- **Colors** and **Structure** (corner radii, heights, paddings, fonts) are the two halves of a theme. **Detail** says how far in
  you go: *Basic* is the handful of values a scheme starts from, *Standard* the roles the controls share, *Advanced* what single
  controls and tools (the Entity Outliner, the console, node graphs) have of their own. The search finds a value at any level.
- A value that **follows** another (`follows tier.Low`) moves with it until you give it a value of its own. Type `$` and a key
  into a colour field (`$tier.Low`) to make a value follow another.
- A value in **bold** is one your theme sets; **Reset** takes it away again. **Changed** lists only the values the theme sets.
- **Manage** duplicates, renames, describes and deletes your themes, and **imports** and **exports** theme files: an exported
  file holds every value, so it reads the same on a machine that lacks the theme it was based on. To share a theme, send the
  file; to install one, import it or drop it into `~/.o3de/Themes`.

## A theme file

```json
{
    "name": "Modern Blue Steel",
    "description": "...",
    "author": "...",
    "base": "O3DE Original",
    "colors": { "tier.FarBG": "#0F1014", "field.Line": "$tier.FarBG" },
    "structure": { "field.Radius": 4, "field.Margin": "2 2 2 2", "surface.Family": "Open Sans" }
}
```

- A token the file does not name has the value of the `base` theme, and below every theme lie the values in the code.
- A colour is `#RRGGBB`, or `#RRGGBBAA` where it is translucent. A length is a number. Margins are `left top right bottom`.
  `$key` follows the token of that key.
- The keys are listed in `Components/Elements/Foundations.md` with their O3DE Original values, and shown in the editor's tool tips.
- An engine theme goes into this folder and into `Components/resources.qrc`. They are never written to.

## The pieces

| Piece | Where | What it is |
|---|---|---|
| Tokens | `Components/Theme/ThemeToken.h` | `ThemeColor`, `ThemeInt`, `ThemeMargins`, `ThemeText`: a value with a key, a label and a level |
| The catalogue | `Components/Elements/Foundations.h` | Every token: the tiers and the palette, the families, the tools; `Foundations.md` is generated from it |
| The registry | `Components/Theme/ThemeTokens.h` | Every token by key; writes a theme's values; builds the `Themed` values again |
| The themes | `Components/Theme/ThemeManager.h` | The engine's and the user's theme files, the one in effect, editing, import and export |
| The editor | `Components/Theme/ThemeEditor.h` | The widget: an Editor tool pane and a window of the Gallery |
| The engine's themes | this folder, `*.theme.json` | Listed in `Components/resources.qrc`; read from this folder in a source build |
| A user's themes | `~/.o3de/Themes` | `*.theme.json`, and `themes.ini` with the theme chosen last |

The O3DE Original theme has no file: it is the values written in the code, which are what the legacy style sheets granted. Every
other theme lies over it.

## Reading a token: three ways, and no other

1. **Code that runs when a widget is polished or painted reads the token itself.** It has the value of the theme in effect.

   ```cpp
   painter->fillRect(rect, Foundations::Mid);
   ```

2. **A family's name for a token is a reference.**

   ```cpp
   const QColor& ToolBarFill = Foundations::Mid;       // ToolBar.qss:20 $fill.bar.idle.Mid
   ```

3. **A value with static storage that is built from tokens is `Themed`.** It is built again whenever the theme changes, at the same
   address, so registries and widgets keep their pointers to it.

   ```cpp
   Themed<BoxElement> ToolBarPanel = [] { return BoxElement{
       .m_name = "Global.Box.ToolBar",
       .m_idle = { .m_fill = QBrush(ToolBarFill) },
   }; };
   ```

A plain copy keeps the value it was built with, and no theme reaches it:

```cpp
const QColor ToolBarFill = Foundations::Mid;           // WRONG: a copy made before any theme
static const QPen pen(Foundations::Line::Dark);        // WRONG inside a function too: built once
inline static const QColor s_fill = MyFlat::Fill;      // WRONG in a class as well: make it a reference
const BoxElement Frame = formFrame("Frame", 50);       // WRONG: formFrame reads a token in its body, and runs once here
const TextElement Title{ .m_pixelSize = 14 };          // WRONG: the family and the weight it leaves out default to tokens
QColor m_fill = MyFlat::Fill;                          // a member of a long-lived object: a reference, or read at paint
```

`constexpr` cannot read a token (the compiler says so). The other copies compile; the checks below find them.

Three element types have members that default to a token: `TextElement` (`m_family`, `m_weight`), `ItemViewElement`
(`m_alternate`, `m_selection`, `m_selectionText`, `m_headerFill`) and `FrameElement` (`m_foreground`). An instance holds those
values from the moment it is made, so one with static storage is `Themed` unless it sets every one of them itself.

What an object takes from a token when it is MADE stays until it is made again: a member with a default
(`const QColor m_fill = MyFlat::Fill;` in a delegate), a size or a margin set in a constructor, a pixmap it rendered, a text with
a link's colour baked in. An application may also make its windows before it has its theme (the Project Manager does). So:

- a member that only names a value is a reference: `const QColor& m_fill = MyFlat::Fill;`
- what a widget sets from a token is set when it is polished, read when it paints, or set again on `QEvent::StyleChange` (every
  widget receives it at a change of theme, before its polish; `BrowseEdit` sets its field's padding there);
- a cache made from tokens compares `ThemeTokens::generation()` with the one it was made at (the grid of a node graph does; so
  does the cache of merged item view elements, which makes each of them again at the address its views keep);
- `ScrollBar::setBackgroundColor(area, Fill)` follows the theme by itself where `Fill` is a token handed over by reference: the
  area is kept by the token's key (`ThemeTokens::keyOf`) and has the theme's value at every polish.

`theme_statics.py members <engine>` lists the members and the constructors that read a token, for a review.

## Adding a token

Declare it once in `Foundations.h`, with the value of O3DE Original and where that value comes from:

```cpp
//! ProgressBar.qss:14 $slider.ProgressTrack $fill.progress.idle.Alt1
inline ThemeColor ProgressTrack{ QColor(0x9A, 0x9A, 0x9A), "slider.ProgressTrack", Standard, "Progress bar track" };
```

- The **key** is what a theme file writes: the token's path below `Foundations`, namespace, a dot, the name.
- The **level** says how far into the theme editor the token shows: `Basic`, `Standard`, `Advanced`.
- The **label** is its name in the editor; a sentence after it, on a line of its own, is its tool tip (every `Basic` token has
  one). A `ThemeGroup` beside the tokens gives their group a title.
- A token may **follow** another: write the other token in place of the value. Use it where one purpose was split from another
  that shares its value (the fill of a menu from the tier it sits on): a scheme that moves the tier moves it too, and a theme can
  still set it apart. O3DE Original draws the same either way.
- The value of O3DE Original never changes with a new token: bind the family's constant to it with
  `Tools/ThemeProof/theme_rebind.py`, which refuses a binding that would change a value.
- A tool's own values go into the tools section of `Foundations.h`, so that one catalogue lists every value a theme sets.

Then run `python generate_foundations_catalogue.py` beside `Foundations.h` (it refuses a token whose key is not its root name).

## How a theme takes effect

`ThemeManager` writes the theme's values into the tokens, has the registry build every `Themed` value again, and asks the style
to polish the application again (`Style::applyTheme`): the Configs read the tokens, the application takes its font and palette,
and each widget, after the widgets above it, receives `QEvent::StyleChange` and is polished: the order an application comes up
in. Nothing is added to a paint or to a polish: a token is a plain value, read as a constant was.

An application starts as O3DE Original. One that offers themes calls `ThemeManager::restoreSavedTheme()` after it initialized its
`StyleManager`. Tests never call it, so they always run O3DE Original.

| Environment variable | Effect |
|---|---|
| `O3DE_THEME=<name>` | The theme of this run; nothing is saved |
| `O3DE_THEME_DELAY_MS=<ms>` | With `O3DE_THEME`: start as O3DE Original and change after this long (a test of the change itself) |
| `O3DE_THEMES_FOLDER=<folder>` | The folder of the user's themes for this run (captures and benchmarks leave the user's own alone) |

## What a theme does not reach

- The legacy (style sheet) GUI.
- Application icons and the art of a control that is an image (a theme tints the arrows and marks inside an entry field; the
  other images keep their colours). A light scheme therefore needs more than colours: the icons are drawn for dark surfaces.
- Colours of content: node graphs' nodes, curve and track editors, the viewport, colour pickers.
- Sites that still set a style sheet of their own text, and the literal colours `Tools/ThemeProof/theme_literals.py` lists.
- The links inside a rich-text document that was read before the change (a label reads its text again; a text edit's document is
  its user's and keeps its colours).
- An application that is running while another changes the theme: each application reads the choice, and a theme's file, when
  it starts.

## Checks

In the project that carries the flatten tooling (`Tools/ThemeProof`, its README):

- `theme_statics.py check <engine>`: no declaration with static storage, at namespace scope, in a function or in a class, copies
  a token: by naming one, by calling a function that reads one, or by being of a type that defaults a member to one.
- `theme_literals.py <engine>`: the literal colours of the flat code that equal a token (candidates to follow it).
- The neon theme (`theme_neon.py`, `theme_reach.py`): every colour token at a hue of its own; what a screen still shows in its
  original colour is what no token reaches yet. Against a build with the hues as the tokens' own values (`theme_reach.py stale`),
  a pixel that shows a token's original colour is a copy no check of the sources found.
- `theme_live_check.py`: a run changed live holds, widget by widget, what a run under the theme from the start holds.
- `Tests/ThemeTests.cpp`: the registry, theme files, the manager, Recolor, the editor's rows.
