# O3DE Editor Theming System -- Architecture & Contributor Guide

This document describes the data-driven editor theming system: its goals, how a token
travels from a theme file to a pixel on screen, every place in the engine it touches, and --
most importantly -- the non-obvious gotchas of making a value *actually* change the UI.

If you only read one section, read **"Gotchas: making a value affect the UI"**. That is where
contributors lose the most time.

---

## 1. Core goals

- **One swappable source of truth.** Replace scattered hard-coded QSS values and per-widget
  `*Config.ini` metrics with references into a central theme, so the entire editor's look is
  driven by a single set of named tokens.
- **The current look is a theme, not the baseline.** `O3DE_Original` reproduces the classic
  editor pixel-for-pixel. Every token's Original value equals the historic hard-coded default,
  so selecting Original is a no-op visually. This is the **value-preserving invariant**.
- **Community-extensible.** Themes are plain JSON folders. A new theme is a new folder. The
  in-editor Theme Editor authors and live-previews them with no rebuild.
- **Token parity.** Every theme defines the *same set* of tokens. A missing token has no
  defined fallback per-theme, so the rule is: **whatever token you add, add it to all themes.**

---

## 2. Architecture / data flow

```
  Themes/<Name>/themeProperties.json        (nested JSON tree of named tokens)
            |
            |  loaded on theme select / startup
            v
  StyleManager  ----implements---->  StyleManagerInterface   (AZ::Interface, RTTI)
     |                                   ^        ^
     |  feeds                            |        |  GetStylePropertyAsColor / ...Integer /
     |                                   |        |  ...String / IsStylePropertyDefined
     v                                   |        |
  StylesheetPreprocessor          (channel A)  (channel B)
     | substitutes $Token in .qss   QSS files   C++ runtime reads (paint time)
     v
  Qt stylesheet applied to widgets
```

A token reaches the screen through **one of two channels**:

### Channel A -- QSS `$Token` substitution (the default, preferred path)
`StylesheetPreprocessor` scans every `.qss` for `$TokenName` and replaces it with the active
theme's value before Qt parses the stylesheet. Use this for anything a stylesheet can express:
widget backgrounds, borders, radii, padding/margins, fonts, selection colors on standard
item-views, etc.

- Token prefix is **`$`** (not `@` -- Qt6's `StyleSheetCache` MiniLessParser eats `@` for
  `@import` detection; `$` sidesteps it).
- Nested JSON keys flatten by concatenation: `{"Radius": {"Input": "4px"}}` -> `$RadiusInput`.
- A metric token may hold 1/2/4 space-separated px components ("8px", "6px 18px",
  "6px 18px 6px 9px"); the whole string substitutes into one `$Token`.

### Channel B -- C++ runtime token reads (for surfaces QSS cannot reach)
Some surfaces are painted by C++ (`QPainter`) and never consult the stylesheet -- custom item
delegates, `QGraphicsItem` scenes (GraphCanvas / EMotionFX), viewport overlays, and any view
whose *content area* is drawn directly. For these, read the token in the paint path:

```cpp
#include <AzCore/Interface/Interface.h>
#include <AzQtComponents/Components/StyleManagerInterface.h>

if (auto* sm = AZ::Interface<AzQtComponents::StyleManagerInterface>::Get();
    sm && sm->IsStylePropertyDefined("MyTokenName"))
{
    const QColor c = sm->GetStylePropertyAsColor("MyTokenName");
    if (c.isValid())
    {
        // use c
    }
}
```

---

## 3. The Theme Editor (in-editor authoring)

`Code/Editor/ThemeEditor/ThemeEditorWidget.*` is a dockable editor pane that reads the active
theme's tokens and renders editors for them (color swatches; 1/2/4-component metric spin-box
editors with T/R/B/L labels), grouped into responsive columns. Two pages:
- **Colors** -- curated semantic categories.
- **Structure** -- widget-based categories (Spin Box, Combo Box, Tabs, Cards, ...).
Edits apply live through `StyleManager`'s refresh path. This is the intended way to discover
un-themed (still-grey) surfaces and to author new themes including `Modern_Blue_Steel`.

---

## 4. Contact-point index

### Core infrastructure
| File | Role |
|---|---|
| `Components/StyleManagerInterface.h` | AZ RTTI interface: `GetStylePropertyAsColor/Integer/String`, `IsStylePropertyDefined`. |
| `Components/StyleManager.cpp/.h` | Loads theme JSON into the property map; owns theme switch + Refresh; `setTheme(name)`. |
| `Components/StylesheetPreprocessor.cpp/.h` | Substitutes `$Token` in QSS via the interface. |
| `Components/ConfigHelpers.cpp/.h` | `themeColor/themeInt/themeReal` shared helpers for `loadConfig`. |
| `Themes/<Name>/themeProperties.json` | The themes: `O3DE_Original`, `O3DE_Dark`, `O3DE_Light`, `Modern_Blue_Steel`. |
| `Code/Editor/ThemeEditor/ThemeEditorWidget.cpp/.h` | In-editor theme authoring pane. |

### QSS token consumers (Channel A) -- representative
- `Components/Widgets/*.qss` (PushButton, LineEdit, ComboBox, SpinBox, Slider, TabWidget,
  Card, TableView, BreadCrumbs, ProgressBar, DragAndDrop, ...) -- fully tokenized.
- `Components/Widgets/BaseStyleSheet.qss` -- global rules (e.g. status-bar item transparency).
- `Code/Editor/Style/Editor.qss` -- editor-window-level surfaces (asset browser body, viewport
  title bar, inspector dark box, console, status bar).
- `UI/Outliner/EntityOutliner.qss` -- outliner item/branch hover + selection tokens.

### C++ runtime token reads (Channel B)
| Area | File |
|---|---|
| Push buttons (secondary set) | `Components/Widgets/PushButton.cpp` |
| Asset grid bg + tiles | `Components/Widgets/AssetFolderThumbnailView.cpp` |
| Tab widget | `Components/Widgets/TabWidget.cpp` |
| Status bar / console | `Code/Editor/MainStatusBar.cpp`, `Controls/ConsoleSCB.cpp` |
| Viewport title bar | `Code/Editor/ViewportTitleDlg.cpp` |
| Title-bar logo tint | `Code/Editor/CryEdit.cpp` |
| Outliner selection/hover (name cell) | `UI/Outliner/EntityOutlinerListModel.cpp` |
| Prefab rows | `UI/Prefab/PrefabUiHandler.cpp` |
| Property rows | `UI/PropertyEditor/PropertyRowWidget.cpp` |
| Asset browser tree/root/favorites | `AssetBrowser/Views/AssetBrowserTreeView.cpp`, `Views/EntryDelegate.cpp`, `Favorites/FavoritesEntryDelegate.cpp`, `Favorites/AssetBrowserFavoritesView.cpp`, `AssetBrowserEntityInspectorWidget.cpp` |
| Atom tools (Material Editor/Canvas) | `AtomToolsFramework/.../AssetSelection/AssetSelectionGrid.cpp`, `Inspector/InspectorGroupHeaderWidget.cpp` (+ theme read in `AtomToolsApplication.cpp`) |
| GraphCanvas (Landscape/Material/Script Canvas) | `GraphCanvas/.../GridVisualComponent.cpp` (scene bg), `Nodes/General/GeneralNodeFrameComponent.cpp` (node body), `Widgets/GraphCanvasLabel.cpp` (node text) |
| EMotionFX | `AnimGraph/NodeGraphWidget.cpp` (graph bg), `AnimGraph/NodeGraph.cpp` (grid), `TimeView/TrackDataWidget.cpp` (time view bg) |
| PhysX PVD banner | `PhysX/Core/Code/Editor/DocumentationLinkWidget.cpp` |

### Top-bar / chrome (non-token but theme-adjacent)
- `Code/Editor/Core/EditorActionsHandler.cpp/.h` -- play-controls block + spacer layout.
- `Components/Titlebar.cpp/.h` -- `setIcon` for the title-bar logo.

---

## 5. Gotchas: making a value affect the UI

These are the failure modes that cost the most time. Internalize them before adding a token.

### G1 -- Config/ctor reads are STALE; paint reads are LIVE
Per-widget `loadConfig()` and most constructors run at **style init, before the theme JSON is
loaded**. A token read there returns the *default* (grey), not the theme value. This is the
single most common "I themed it but it's still grey" bug.
- **Fix:** read the token at **paint time** (in `paintEvent` / delegate `paint` / `QGraphicsItem::paint`),
  not in the config or constructor. By paint time the theme is always loaded.
- A `loadConfig` token read is acceptable *only* for surfaces re-polished after theme switch;
  otherwise prefer paint-time.

### G2 -- QSS does not reach C++-painted surfaces
A stylesheet rule on a view does **not** color content that the view paints itself: the
viewport render surface, an item-view *viewport background*, `QGraphicsScene` content, custom
delegates. Symptoms: the frame themes but the interior stays grey.
- **Fix:** Channel B. `fillRect(viewport()->rect(), color)` in `paintEvent`, or read the token
  in the delegate/graphics-item paint. (See `AssetFolderThumbnailView`, GraphCanvas.)

### G3 -- Custom delegates suppress QSS selection
If a delegate custom-paints a column, the stylesheet's `::item:selected` background never
reaches that cell -- the rest of the row highlights, that cell does not. (This was the outliner
name-cell regression.)
- **Fix:** paint that cell's highlight in the delegate using the *same* selection token the QSS
  uses, scoped to the custom column only (so default-painted columns don't double-paint).

### G4 -- Value-preserving Original (the no-op trick)
When a runtime read overrides a hard-coded color, set the **Original** token to the *exact
original value* so Original stays pixel-identical. If the override is conditional (e.g. only
re-target dark text), make Original's value a no-op (e.g. `#000000` for a "darken only" path).
Only the *new* theme carries the visible value.

### G5 -- Token parity is mandatory
Add every new token to **all** theme JSONs (`O3DE_Original`, `O3DE_Dark`, `O3DE_Light`,
`Modern_Blue_Steel`). A token present in one theme and absent in another yields
`IsStylePropertyDefined == false` for the others -> the override silently no-ops there.

### G6 -- Separate apps need to read the shared theme
Atom Tools (Material Editor/Canvas, etc.) are separate applications; each creates its own
`StyleManager` defaulting to `O3DE_Original`. They must read the editor's chosen theme from
shared settings (`QSettings("O3DE","O3DE Editor")`, key `Settings/EditorTheme`) and call
`StyleManager::setTheme(...)` after initialize. See `AtomToolsApplication.cpp`.

### G7 -- Always guard the read
`IsStylePropertyDefined(name)` then validity-check the result (`color.isValid()`), and fall
back to the historic literal. This keeps the code correct under a theme that omits the token
and preserves Original behavior.

### G8 -- NO UNICODE, ever
The CI validator rejects any non-ASCII byte in any source / qss / json file. No smart quotes,
arrows, em-dashes, or glyphs. Use ASCII (`->`, `--`, `[V]`). This has bitten multiple edits.

---

## 6. How to contribute a themed surface (checklist)

1. **Can a stylesheet express it?** If yes -> Channel A: add `$Token` to the relevant `.qss`,
   define the token in every theme JSON. Done.
2. **Is it C++-painted (grey despite QSS)?** -> Channel B: read the token at *paint time*
   (G1), guarded (G7), with Original set to the historic literal (G4).
3. **Add the token to all four theme JSONs** with matching nesting (G5). Keep Original's value
   equal to the pre-existing hard-coded value.
4. **Verify parity** (same leaf-token count across themes) and **zero non-ASCII** (G8).
5. **Rebuild** for any C++ change; QSS/JSON-only changes can hot-reload via the Theme Editor.
6. Confirm: Original looks identical to before; the new theme shows your value.
