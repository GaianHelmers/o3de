<!--
Generated from Foundations.h by generate_foundations_catalogue.py. Do not edit: change the header, then run the script.
-->

# Foundations: the token catalogue of the flattened GUI

The foundations of the flattened GUI: the values many families stand on, each defined once. A rule of the legacy sheets that reached many families lives here, and a family uses it from here; a family's own value that sits on a primitive layer keeps its own name and token id and takes its value from the layer. A theme that shifts a foundation reaches every feature on it.

Ids. A token's root name mirrors its path here: $tier.&lt;Name&gt; for a tier, $&lt;namespace&gt;.&lt;Name&gt; for the rest (Text::Primary is $text.Primary, Field::HoverLine is $field.HoverLine). Its purpose ids name what it is for, as the legacy sheets granted it: $&lt;channel&gt;.&lt;role&gt;.&lt;state&gt;[.&lt;attribute&gt;][.&lt;tier&gt; | .Clear | .AltN], where

- channel    is fill, text, line, type, space, size, radius, asset or other;
- role       is the widget the rule styles (button, label, field, tab, surface, view, item ...), a named variant of it joined by a hyphen (lock-toggle, tab-pane);
- state      is idle, a pseudo-state (hover, pressed, focus, checked, selected, disabled), or the variant a selector tests as the sheet writes it (primary, drawSimple, HasError), joined to a pseudo-state by + (hover+primary);
- attribute  is the sheet's property (min-height, margin-top), a Qt property in lower case (iconsize, minimumsize), or a hyphenated name for a value Qt computes (box-pad, inner-contrast);
- the last   part is the tier the value sits on, Clear, or AltN for the n-th value of one purpose.

The sections follow the header, from the primitive tiers down to the values derived from them.

## Primitive layers

The background tiers of the BJDS grey ladder, darkest first, and no paint.

| Token | Root name | Value | Reads | Purpose ids | Source | Note |
|---|---|---|---|---|---|---|
| `FarBG` | `$tier.FarBG` | #111111 |  |  | BJDS Gray Far BG |  |
| `BG` | `$tier.BG` | #222222 |  |  | BJDS Gray BG |  |
| `Low` | `$tier.Low` | #333333 |  |  | BJDS Gray Low |  |
| `Mid` | `$tier.Mid` | #444444 |  |  | BJDS Gray Mid |  |
| `Highest` | `$tier.Highest` | #555555 |  |  | BJDS Gray Highest |  |
| `Clear` | `$tier.Clear` | #00000000 |  |  | Qt::transparent |  |

## The palette

The colours several families share, each defined once. A family's or a role's value that is one of them takes it from here, as the tiers above.

| Token | Root name | Value | Reads | Purpose ids | Source | Note |
|---|---|---|---|---|---|---|
| `Text::Primary` | `$text.Primary` | #FFFFFF |  |  | BaseStyleSheet.qss:15, PushButton.qss:37 |  |
| `Text::Disabled` | `$text.Disabled` | #999999 |  |  | PushButton.qss:43, LineEdit.qss:28, Card.qss:272 |  |
| `Text::OnLight` | `$text.OnLight` | #000000 |  |  | BaseStyleSheet.qss:31 (the entry fields) |  |
| `Text::Muted` | `$text.Muted` | #888888 |  |  | Text.qss:16 |  |
| `Text::DisabledItem` | `$text.DisabledItem` | #555555 |  | $text.item.disabled.Highest | Menu.qss:25, ComboBox.qss:110 | Fragment of Highest: the text of a disabled menu or popup item Menu.qss:25, ComboBox.qss:110 |
| `Accent::Primary` | `$accent.Primary` | #1E70EB |  |  | BaseStyleSheet.qss:55, Splitter.qss:21, ProgressBar.qss:21 |  |
| `Accent::Focus` | `$accent.Focus` | #94D2FF |  |  | BaseStyleSheet.qss:107, SpinBox.qss:42, ComboBox.qss:31 |  |
| `Accent::Selected` | `$accent.Selected` | #008DE5 |  |  | TabWidget.qss:158 |  |
| `Accent::Error` | `$accent.Error` | #E25243 |  |  | SpinBox.qss:57, ComboBox.qss:44 |  |
| `Accent::Info` | `$accent.Info` | #4285F4 |  |  | VectorInput.qss:47 |  |
| `Accent::Success` | `$accent.Success` | #58BC61 |  |  | VectorInput.qss:42 |  |
| `Accent::PrimaryLight` | `$accent.PrimaryLight` | #0095F2 |  | $fill.button.primary | The light top of a primary button's gradient: PushButtonConfig.ini:22, ProjectManager.qss:17 |  |
| `Fill::Disabled` | `$fill.Disabled` | #666666 |  |  | LineEdit.qss:28, SpinBox.qss:69, ComboBox.qss:53 |  |
| `Fill::ItemSelected` | `$fill.ItemSelected` | #656565 |  |  | TableView.qss:42, :14 |  |
| `Fill::ItemHeader` | `$fill.ItemHeader` | #2D2D2D |  |  | TableView.qss:62 |  |
| `Fill::ItemView` | `$fill.ItemView` | #4D4D4D |  |  | TableView.qss:14 |  |
| `Fill::FieldLight` | `$fill.FieldLight` | #E9E9E9 |  |  | BaseStyleSheet.qss:90, AssetsTab.qss:38 |  |
| `Fill::HoverOverlay` | `$fill.HoverOverlay` | #FFFFFF33 |  |  | TabWidget.qss:70 |  |
| `Fill::Deepest` | `$fill.Deepest` | #000000 |  |  | ToolTip.qss:13, ProjectManager.qss:462 |  |
| `Fill::ButtonLight` | `$fill.ButtonLight` | #777777 |  | $fill.button.hover.gradient $fill.button.disabled+primary $fill.button.pressed+secondary | The light grey of a button's gradient: PushButtonConfig.ini:31-32, BrowseEdit.qss:77, ProjectManager.qss:29, :44 |  |
| `Fill::ButtonHighlight` | `$fill.ButtonHighlight` | #888888 |  | $fill.button.secondary | The lighter grey a button's gradient starts from: PushButtonConfig.ini:30, ProjectManager.qss:35 |  |
| `Line::Dark` | `$line.Dark` | #000000 |  |  | ScrollBar.qss:27, WindowDecorationWrapper.qss:9 |  |
| `Line::Frame` | `$line.Frame` | #1B1B1B |  |  | InspectorGroupHeaderWidget.cpp:26, AssetBundler.qss:34 |  |
| `Line::TabPane` | `$line.TabPane` | #2B2B2B |  |  | TabWidget.qss:111 |  |
| `Line::Light` | `$line.Light` | #FFFFFF |  | $line.button.idle.color.Alt2 $line.thumbnail-child.selected.color $line.gradient-slider-handle.idle.color | The white outline: ProjectManager.qss:631, :687, AssetFolderThumbnailViewConfig.ini:26, SliderConfig.ini:11 |  |
| `Line::Segment` | `$line.Segment` | #222222 |  | $line.button.idle.color.BG | SegmentControl.qss:17 | Fragment of BG: the line of a segment control |
| `Line::ColorPickerFrame` | `$line.ColorPickerFrame` | #333333 |  | $line.surface.idle.color.Low | ColorPicker.qss:108 | Fragment of Low: the frames of the colour picker's grid, preview and swatches ColorPicker.qss:108 |

## The universal rule

BaseStyleSheet.qss's `*`, which reached every widget.

| Token | Root name | Value | Reads | Purpose ids | Source | Note |
|---|---|---|---|---|---|---|
| `Surface::Fill` | `$surface.Fill` | #00000000 | `Clear` | $fill.surface.idle.Clear | BaseStyleSheet.qss:17 |  |
| `Surface::Text` | `$surface.Text` | #FFFFFF | `Text::Primary` | $text.surface.idle.Alt1 | BaseStyleSheet.qss:18 |  |
| `Surface::Family` | `$surface.Family` | "Open Sans" |  | $type.surface.idle.family | BaseStyleSheet.qss:19 |  |
| `Surface::Weight` | `$surface.Weight` | 400 |  | $type.surface.idle.weight | BaseStyleSheet.qss:20 |  |
| `Surface::Size` | `$surface.Size` | 12 |  | $type.surface.idle.size | StyleManager::initialize (the application font), ColorPicker.qss:18 (`ColorPicker *`), Text.qss:92 (`.Label`) |  |

## The label rule

Text.qss's `.Label, QLabel`; the tiers of Text.qss and TitleBar.qss restate its family and size.

| Token | Root name | Value | Reads | Purpose ids | Source | Note |
|---|---|---|---|---|---|---|
| `Label::Family` | `$label.Family` | "Open Sans" |  | $type.label.idle.family.Alt1 | Text.qss:91 (QLabel), :57, :66, :74, :83, :99, :114, :122 (the tiers), TitleBar.qss:33 |  |
| `Label::Size` | `$label.Size` | 12 |  | $type.label.idle.size.Alt1 | Text.qss:92, :84, :100, :115, :123 |  |
| `Label::MarginV` | `$label.MarginV` | 1 |  | $space.label.idle.margin-top.Alt1 $space.label.idle.margin-bottom.Alt1 | Text.qss:93-94 |  |

## The entry field

BaseStyleSheet.qss's rule of the entry controls (QTextEdit, QPlainTextEdit, QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox), and what LineEdit.qss, ComboBox.qss and SpinBox.qss share.

| Token | Root name | Value | Reads | Purpose ids | Source | Note |
|---|---|---|---|---|---|---|
| `Field::Fill` | `$field.Fill` | #CCCCCC |  | $fill.field.idle.Alt1 | BaseStyleSheet.qss:76, ComboBox.qss:19 |  |
| `Field::Text` | `$field.Text` | #000000 | `Text::OnLight` | $text.field.idle.Alt1 | BaseStyleSheet.qss:77 |  |
| `Field::Family` | `$field.Family` | "Open Sans" |  | $type.field.idle.family | BaseStyleSheet.qss:78 |  |
| `Field::Radius` | `$field.Radius` | 2 |  | $radius.field.idle.all | BaseStyleSheet.qss:79 |  |
| `Field::Line` | `$field.Line` | #E9E9E9 |  | $line.field.idle.color.Alt1 | BaseStyleSheet.qss:81 |  |
| `Field::Size` | `$field.Size` | 12 |  | $type.field.idle.size.Alt1 | BaseStyleSheet.qss:82 |  |
| `Field::Height` | `$field.Height` | 16 |  | $size.field.idle.height.Alt1 | LineEdit.qss:13, ComboBox.qss:15, SpinBox.qss:12, BrowseEdit.qss:17 |  |
| `Field::Margin` | `$field.Margin` | 2, 2, 2, 2 |  | $space.field.idle.margin.Alt1 | SpinBox.qss:13, BrowseEdit.qss:12 |  |
| `Field::PaddingLeft` | `$field.PaddingLeft` | 0 |  | $space.field.idle.padding-left.Alt1 | LineEdit.qss:15, SpinBox.qss:17 |  |
| `Field::PaddingTop` | `$field.PaddingTop` | -1 |  | $space.field.idle.padding-top.Alt1 | LineEdit.qss:19, SpinBox.qss:19 |  |
| `Field::PaddingBottom` | `$field.PaddingBottom` | 1 |  | $space.field.idle.padding-bottom | LineEdit.qss:18, SpinBox.qss:18 |  |
| `Field::HoverLineWidth` | `$field.HoverLineWidth` | 1 |  | $line.field.hover.width.Alt1 | ComboBox.qss:25, SpinBox.qss:28, BaseStyleSheet.qss:102 |  |
| `Field::HoverMargin` | `$field.HoverMargin` | 1, 1, 1, 1 |  | $space.field.hover.margin.Alt1 | SpinBox.qss:27, BaseStyleSheet.qss:101 |  |
| `Field::HoverLine` | `$field.HoverLine` | #222222 |  | $line.field.hover.color.BG | SpinBox.qss:36, BaseStyleSheet.qss:104 | Fragment of BG: the line of a hovered field SpinBox.qss:36, BaseStyleSheet.qss:104 |
| `Field::HoverRadius` | `$field.HoverRadius` | 3 |  | $radius.field.hover.all | ComboBox.qss:27, SpinBox.qss:30 |  |
| `Field::FocusLineWidth` | `$field.FocusLineWidth` | 1 |  | $line.field.focus.width.Alt1 | ComboBox.qss:36, SpinBox.qss:28, BaseStyleSheet.qss:112 |  |
| `Field::FocusMargin` | `$field.FocusMargin` | 1, 1, 1, 1 |  | $space.field.focus.margin.Alt1 | SpinBox.qss:27, BaseStyleSheet.qss:111 |  |
| `Field::FocusRadius` | `$field.FocusRadius` | 3 |  | $radius.field.focus.all | ComboBox.qss:38, SpinBox.qss:30 |  |
| `Field::FocusLine` | `$field.FocusLine` | #94D2FF | `Accent::Focus` | $line.field.focus.color.Alt1 | ComboBox.qss:39, SpinBox.qss:45, BaseStyleSheet.qss:113 |  |
| `Field::FocusFill` | `$field.FocusFill` | #FFFFFF |  | $fill.field.focus.Alt1 | ComboBox.qss:41, SpinBox.qss:46, BrowseEdit.qss:41 |  |
| `Field::ErrorMargin` | `$field.ErrorMargin` | 0, 0, 0, 0 |  | $space.field.HasError | ComboBox.qss:46, SpinBox.qss:62 |  |
| `Field::ErrorLineWidth` | `$field.ErrorLineWidth` | 2 |  | $line.field.HasError.width | ComboBox.qss:47, SpinBox.qss:63 |  |
| `Field::ErrorRadius` | `$field.ErrorRadius` | 4 |  | $radius.field.HasError | ComboBox.qss:49, SpinBox.qss:65 |  |
| `Field::ErrorLine` | `$field.ErrorLine` | #E25243 | `Accent::Error` | $line.field.HasError.color | ComboBox.qss:50, SpinBox.qss:66 |  |
| `Field::DisabledFill` | `$field.DisabledFill` | #666666 | `Fill::Disabled` | $fill.field.disabled | LineEdit.qss:33, ComboBox.qss:55, SpinBox.qss:72 |  |
| `Field::DisabledText` | `$field.DisabledText` | #999999 | `Text::Disabled` | $text.field.disabled.Alt1 | LineEdit.qss:34, ComboBox.qss:56, SpinBox.qss:73 |  |

## The secondary tab

TabWidget.qss's `AzQtComponents--TabBar.Secondary`, which TabElement draws and Script Canvas's graph tabs restate.

| Token | Root name | Value | Reads | Purpose ids | Source | Note |
|---|---|---|---|---|---|---|
| `Tab::SecondaryText` | `$tab.SecondaryText` | #959595 |  | $text.tab.idle | TabWidget.qss:142 |  |
| `Tab::SecondaryHoverFill` | `$tab.SecondaryHoverFill` | #FFFFFF26 |  | $fill.tab.hover.Alt1 | TabWidget.qss:147 |  |
| `Tab::SecondaryLine` | `$tab.SecondaryLine` | #333333 |  | $line.tab.idle.color.Low | TabWidget.qss:140 | Fragment of Low: the underline of an idle tab |

## Fragments

A fragment equals a tier's value but serves another purpose, so it is a token of its own: a theme that changes the
tier leaves it as it is.

| Token | Value | Fragment of | Purpose |
|---|---|---|---|
| `Text::DisabledItem` | #555555 | `Highest` | the text of a disabled menu or popup item Menu.qss:25, ComboBox.qss:110 |
| `Line::Segment` | #222222 | `BG` | the line of a segment control |
| `Line::ColorPickerFrame` | #333333 | `Low` | the frames of the colour picker's grid, preview and swatches ColorPicker.qss:108 |
| `Field::HoverLine` | #222222 | `BG` | the line of a hovered field SpinBox.qss:36, BaseStyleSheet.qss:104 |
| `Tab::SecondaryLine` | #333333 | `Low` | the underline of an idle tab |

## Derived

The values an element or Qt computes from the tokens above. None is a constant of its own: each follows its base token, as it did under the legacy sheets. An entry reads: id | from: its base | the formula | where it shows.


### An element's shades (setShades in ElementCommon.cpp, as QRenderRule::configurePalette shades a rule's background; the factors are tokens there)

| Id | From | Formula | Where it shows |
|---|---|---|---|
| `$line.shade.idle.light` | the element's fill | fill.lighter(115) | Light: a sunken separator's light line, #4E4E4E on Mid |
| `$line.shade.idle.midlight` | the element's fill | fill.lighter(107) | Midlight |
| `$line.shade.idle.dark` | the element's fill | fill.darker(150) | Dark: the separator's dark line, #2D2D2D on Mid |
| `$line.shade.idle.shadow` | the element's fill | fill.darker(300) | Shadow |

### Fusion's colours, where the base style draws over the palette an element gave the widget (QFusionStylePrivate::outline, QFusionStylePrivate::tabFrameColor)

| Id | From | Formula | Where it shows |
|---|---|---|---|
| `$line.surface.idle.color.Alt6` | Window, a panel's fill (Mid) | outline.lighter(108), outline = Window.darker(140) (QFusionStylePrivate::outline) | a frame's line (QFusionStyle::drawPrimitive, PE_Frame), #343434 |
| `$fill.tab-pane.idle.Alt2` | Button, the plain tab fill (Mid) | QFusionStylePrivate::tabFrameColor: buttonColor lightened by 4 percent, buttonColor = Button.lighter(100 + max(1, (180 - gray) / 6)) at three quarters of its saturation (QFusionStylePrivate::buttonColor) | a plain tab widget's pane (PE_FrameTabWidget), #535353 |
| `$line.tab-pane.idle.color.Alt2` | Window (Mid) | outline = Window.darker(140) | that pane's border, #303030 |

### Fusion's own shades, Qt constants drawn over the tokens (QFusionStylePrivate::lightShade, darkShade, topShadow, innerContrastLine)

| Id | From | Formula | Where it shows |
|---|---|---|---|
| `$fill.bevel.idle.light-shade` | none, a Qt constant | white at alpha 90 (lightShade) | a splitter's grip (QFusionStyle::drawControl, CE_Splitter) |
| `$fill.bevel.idle.dark-shade` | none, a Qt constant | black at alpha 60 (darkShade) | the same grip |
| `$line.bevel.idle.top-shadow` | none, a Qt constant | black at alpha 18 (topShadow) | Fusion's top shadows |
| `$line.bevel.idle.inner-contrast` | none, a Qt constant | white at alpha 30 (innerContrastLine) | a pane's inner line, #676767 over #535353 |
| `$line.bevel.idle.pane-shadow` | none, a Qt constant | black at alpha 15 | the pane's bottom line (PE_FrameTabWidget) |

### Rule not located (ledger) (GridGraphicsItem::CacheStencils; materialcanvas_style.json:5, passcanvas_style.json:5 give #222222)

| Id | From | Formula | Where it shows |
|---|---|---|---|
| `$fill.graph.idle` | Mid, in part | rule not located (ledger) | the graph view of Material Canvas and Pass Canvas |
