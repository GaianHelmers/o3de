<!--
Generated from Foundations.h by generate_foundations_catalogue.py. Do not edit: change the header, then run the script.
-->

# Foundations: the token catalogue of the flattened GUI

The foundations of the flattened GUI: the values many families stand on, each defined once. A rule of the legacy sheets that reached many families lives here, and a family uses it from here; a family's own value that sits on a primitive layer keeps its own name and token id and takes its value from the layer. A theme that shifts a foundation reaches every feature on it.

Tokens. Each value here is a token (ThemeToken.h): the value written is the O3DE Original theme's, what the legacy sheets granted, and a theme may give it another while the application runs. A declaration reads

- inline ThemeColor Name{ value or the token it follows, "key", level, "label"[, "where it shows"] }; where the key is the token's root name without the `$`, the level says how far into the theme editor it shows (Basic, Standard, Advanced), the label is its name there, and the sentence after it, on a line of its own, is its tool tip. A token that follows another takes that token's value until a theme gives it one of its own. Read a token where a widget is polished or painted, name it with a reference (`const QColor& ToolBarFill = Foundations::Mid;`), and build what is made from tokens as a Themed value.

Ids. A token's root name mirrors its path here: $tier.&lt;Name&gt; for a tier, $&lt;namespace&gt;.&lt;Name&gt; for the rest (Text::Primary is $text.Primary, Field::HoverLine is $field.HoverLine). Its purpose ids name what it is for, as the legacy sheets granted it: $&lt;channel&gt;.&lt;role&gt;.&lt;state&gt;[.&lt;attribute&gt;][.&lt;tier&gt; | .Clear | .AltN], where

- channel    is fill, text, line, type, space, size, radius, asset or other;
- role       is the widget the rule styles (button, label, field, tab, surface, view, item ...), a named variant of it joined by a hyphen (lock-toggle, tab-pane);
- state      is idle, a pseudo-state (hover, pressed, focus, checked, selected, disabled), or the variant a selector tests as the sheet writes it (primary, drawSimple, HasError), joined to a pseudo-state by + (hover+primary);
- attribute  is the sheet's property (min-height, margin-top), a Qt property in lower case (iconsize, minimumsize), or a hyphenated name for a value Qt computes (box-pad, inner-contrast);
- the last   part is the tier the value sits on, Clear, or AltN for the n-th value of one purpose.

The sections follow the header, from the primitive tiers down to the values derived from them. Key, Level and Label are
what a theme file and the theme editor use; a row without a key is a constant, which no theme changes.

## Primitive layers

The background tiers of the BJDS grey ladder, darkest first, and no paint.

| Token | Root name | Key | Level | Label | Value | Follows | Purpose ids | Source | Note |
|---|---|---|---|---|---|---|---|---|---|
| `FarBG` | `$tier.FarBG` | `tier.FarBG` | Basic | Farthest background | #111111 |  |  | BaseStyleSheet.qss:52, TitleBar.qss:11 BJDS Gray Far BG |  |
| `BG` | `$tier.BG` | `tier.BG` | Basic | Background | #222222 |  |  | Splitter.qss:10, Menu.qss:12 BJDS Gray BG |  |
| `Low` | `$tier.Low` | `tier.Low` | Basic | Low surface | #333333 |  |  | QDockWidget.qss:22, Card.qss:129 BJDS Gray Low |  |
| `Mid` | `$tier.Mid` | `tier.Mid` | Basic | Middle surface | #444444 |  |  | BaseStyleSheet.qss:29, :65 BJDS Gray Mid |  |
| `Highest` | `$tier.Highest` | `tier.Highest` | Basic | Highest surface | #555555 |  |  | Card.qss:23, MenuBar.qss:30 BJDS Gray Highest |  |
| `Clear` | `$tier.Clear` |  |  |  | #00000000 |  |  | Qt::transparent |  |

## The palette

The colours several families share, each defined once. A family's or a role's value that is one of them takes it from here, as the tiers above.

| Token | Root name | Key | Level | Label | Value | Follows | Purpose ids | Source | Note |
|---|---|---|---|---|---|---|---|---|---|
| `Text::Primary` | `$text.Primary` | `text.Primary` | Basic | Text | #FFFFFF |  |  | BaseStyleSheet.qss:15, PushButton.qss:37 |  |
| `Text::Disabled` | `$text.Disabled` | `text.Disabled` | Basic | Disabled text | #999999 |  |  | PushButton.qss:43, LineEdit.qss:28, Card.qss:272 |  |
| `Text::OnLight` | `$text.OnLight` | `text.OnLight` | Standard | Text on a light fill | #000000 |  |  | BaseStyleSheet.qss:31 (the entry fields) |  |
| `Text::Muted` | `$text.Muted` | `text.Muted` | Standard | Muted text | #888888 |  |  | Text.qss:16 |  |
| `Text::TextArea` | `$text.TextArea` | `text.TextArea` | Standard | Text area text | #545454 |  | $text.field.idle.Alt2 | BaseStyleSheet.qss:91 (the text edits) |  |
| `Text::Highlighted` | `$text.Highlighted` | `text.Highlighted` | Standard | Highlighted text | #44B2F8 |  | $text.label.idle.Alt6 | Text.qss:21-24 (`.highlightedText`) |  |
| `Text::DisabledItem` | `$text.DisabledItem` | `text.DisabledItem` | Advanced | Disabled menu item | #555555 | `Highest` | $text.item.disabled.Highest | Menu.qss:25, ComboBox.qss:110 | Fragment of Highest: the text of a disabled menu or popup item |
| `Text::DisabledOnFill` | `$text.DisabledOnFill` | `text.DisabledOnFill` | Advanced | Text on a disabled fill | #BBBBBB |  | $text.button.disabled.Alt2 $text.button.default+disabled $text.small-icon.disabled.arrow $fill.check-indicator.checked+disabled.mark | PushButton.qss:63, PushButtonConfig.ini:11 | The text and the mark over a disabled fill, lighter than Disabled: a disabled primary button's text, a small icon button's disabled arrow, the knob and the dot of a disabled indicator that is on (toggleswitch/checked-disabled.svg:9, radiobutton/checked-disabled.svg:8) |
| `Accent::Primary` | `$accent.Primary` | `accent.Primary` | Basic | Accent | #1E70EB |  |  | BaseStyleSheet.qss:55, Splitter.qss:21, ProgressBar.qss:21 |  |
| `Accent::Focus` | `$accent.Focus` | `accent.Focus` | Basic | Focus and links | #94D2FF |  |  | BaseStyleSheet.qss:107, SpinBox.qss:42, ComboBox.qss:31 |  |
| `Accent::Selected` | `$accent.Selected` | `accent.Selected` | Standard | Selected tab | #008DE5 |  |  | TabWidget.qss:158 |  |
| `Accent::Error` | `$accent.Error` | `accent.Error` | Standard | Error | #E25243 |  |  | SpinBox.qss:57, ComboBox.qss:44 |  |
| `Accent::Info` | `$accent.Info` | `accent.Info` | Standard | Information | #4285F4 |  |  | VectorInput.qss:47 |  |
| `Accent::Success` | `$accent.Success` | `accent.Success` | Standard | Success | #58BC61 |  |  | VectorInput.qss:42 |  |
| `Accent::PrimaryLight` | `$accent.PrimaryLight` | `accent.PrimaryLight` | Advanced | Primary button, top | #0095F2 |  | $fill.button.primary | The light top of a primary button's gradient: PushButtonConfig.ini:22, ProjectManager.qss:17 |  |
| `Accent::PrimaryHoverLight` | `$accent.PrimaryHoverLight` | `accent.PrimaryHoverLight` | Advanced | Primary hover, top | #4ABAFF |  | $fill.button.hover+primary.Alt2 | PushButtonConfig.ini:20 | The light top of a hovered primary button's gradient |
| `Accent::PrimaryDisabled` | `$accent.PrimaryDisabled` | `accent.PrimaryDisabled` | Advanced | Disabled accent | #2A4875 |  | $fill.button.disabled+primary.Alt2 $fill.check-indicator.checked+disabled | PushButtonConfig.ini:16-17 | The body of a disabled primary control: a disabled primary button's gradient, a disabled indicator that is on (toggleswitch/checked-disabled.svg:8, radiobutton/checked-disabled.svg:7) |
| `Accent::FocusRing` | `$accent.FocusRing` | `accent.FocusRing` | Standard | Focus ring | #4B8CEF |  | $line.button.focus.color.Alt2 $line.check-indicator.focus.color | PushButtonConfig.ini:45 | The focus ring of a control: a focused push button's border, the ring of a focused indicator, on or off (toggleswitch/unchecked-focus.svg:8, radiobutton/unchecked-focus.svg:7) |
| `Fill::Disabled` | `$fill.Disabled` | `fill.Disabled` | Standard | Disabled control | #666666 |  |  | LineEdit.qss:28, SpinBox.qss:69, ComboBox.qss:53 |  |
| `Fill::ItemSelected` | `$fill.ItemSelected` | `fill.ItemSelected` | Standard | Selected row | #656565 |  |  | TableView.qss:42, :14 |  |
| `Fill::ItemHeader` | `$fill.ItemHeader` | `fill.ItemHeader` | Standard | View header | #2D2D2D |  |  | TableView.qss:62 |  |
| `Fill::ItemView` | `$fill.ItemView` | `fill.ItemView` | Standard | Alternate row | #4D4D4D |  |  | TableView.qss:14 |  |
| `Fill::FieldLight` | `$fill.FieldLight` | `fill.FieldLight` | Standard | Text area | #E9E9E9 |  |  | BaseStyleSheet.qss:90, AssetsTab.qss:38 |  |
| `Fill::HoverOverlay` | `$fill.HoverOverlay` | `fill.HoverOverlay` | Standard | Hover overlay | #FFFFFF33 |  |  | TabWidget.qss:70 |  |
| `Fill::Deepest` | `$fill.Deepest` | `fill.Deepest` | Standard | Tool tip | #000000 |  |  | ToolTip.qss:13, ProjectManager.qss:462 |  |
| `Fill::ButtonLight` | `$fill.ButtonLight` | `fill.ButtonLight` | Advanced | Button, light | #777777 |  | $fill.button.hover.gradient $fill.button.disabled+primary $fill.button.pressed+secondary | The light grey of a button's gradient: PushButtonConfig.ini:31-32, BrowseEdit.qss:77, ProjectManager.qss:29, :44 |  |
| `Fill::ButtonHighlight` | `$fill.ButtonHighlight` | `fill.ButtonHighlight` | Advanced | Button, highlight | #888888 |  | $fill.button.secondary | The lighter grey a button's gradient starts from: PushButtonConfig.ini:30, ProjectManager.qss:35 |  |
| `Fill::ButtonPressed` | `$fill.ButtonPressed` | `fill.ButtonPressed` | Advanced | Button, pressed | #444444 | `Mid` | $fill.button.pressed.Mid | PushButtonConfig.ini:28-29 | Fragment of Mid: a button while it is pressed |
| `Fill::ButtonLow` | `$fill.ButtonLow` | `fill.ButtonLow` | Advanced | Button, low | #666666 | `Fill::Disabled` | $fill.button.secondary.gradient-end | PushButtonConfig.ini:33 | Fragment of Disabled: the bottom of a secondary button's gradient |
| `Line::Dark` | `$line.Dark` | `line.Dark` | Standard | Dark line | #000000 |  |  | ScrollBar.qss:27, WindowDecorationWrapper.qss:9 |  |
| `Line::Frame` | `$line.Frame` | `line.Frame` | Standard | Frame line | #1B1B1B |  |  | InspectorGroupHeaderWidget.cpp:26, AssetBundler.qss:34 |  |
| `Line::TabPane` | `$line.TabPane` | `line.TabPane` | Advanced | Tab pane line | #2B2B2B |  |  | TabWidget.qss:111 |  |
| `Line::Light` | `$line.Light` | `line.Light` | Standard | Light outline | #FFFFFF |  | $line.button.idle.color.Alt2 $line.thumbnail-child.selected.color $line.gradient-slider-handle.idle.color | The white outline: ProjectManager.qss:631, :687, AssetFolderThumbnailViewConfig.ini:26, SliderConfig.ini:11 |  |
| `Line::Segment` | `$line.Segment` | `line.Segment` | Advanced | Segment control line | #222222 | `BG` | $line.button.idle.color.BG | SegmentControl.qss:17 | Fragment of BG: the line of a segment control |
| `Line::ColorPickerFrame` | `$line.ColorPickerFrame` | `line.ColorPickerFrame` | Advanced | Color picker frames | #333333 | `Low` | $line.surface.idle.color.Low | ColorPicker.qss:108 | Fragment of Low: the frames of the colour picker's grid, preview and swatches |

## The universal rule

BaseStyleSheet.qss's `*`, which reached every widget.

| Token | Root name | Key | Level | Label | Value | Follows | Purpose ids | Source | Note |
|---|---|---|---|---|---|---|---|---|---|
| `Surface::Fill` | `$surface.Fill` |  |  |  | #00000000 | `Clear` | $fill.surface.idle.Clear | BaseStyleSheet.qss:17 |  |
| `Surface::Text` | `$surface.Text` | `surface.Text` | Advanced | Text | #FFFFFF | `Text::Primary` | $text.surface.idle.Alt1 | BaseStyleSheet.qss:18 |  |
| `Surface::Family` | `$surface.Family` | `surface.Family` | Standard | Font family | "Open Sans" |  | $type.surface.idle.family | BaseStyleSheet.qss:19 |  |
| `Surface::Weight` | `$surface.Weight` | `surface.Weight` | Advanced | Font weight | 400 |  | $type.surface.idle.weight | BaseStyleSheet.qss:20 |  |
| `Surface::Size` | `$surface.Size` | `surface.Size` | Standard | Font size | 12 |  | $type.surface.idle.size | StyleManager::initialize (the application font), ColorPicker.qss:18 (`ColorPicker *`), Text.qss:92 (`.Label`) |  |

## The label rule

Text.qss's `.Label, QLabel`; the tiers of Text.qss and TitleBar.qss restate its family and size.

| Token | Root name | Key | Level | Label | Value | Follows | Purpose ids | Source | Note |
|---|---|---|---|---|---|---|---|---|---|
| `Label::Family` | `$label.Family` | `label.Family` | Advanced | Font family | "Open Sans" | `Surface::Family` | $type.label.idle.family.Alt1 | Text.qss:91 (QLabel), :57, :66, :74, :83, :99, :114, :122 (the tiers), TitleBar.qss:33 |  |
| `Label::Size` | `$label.Size` | `label.Size` | Advanced | Font size | 12 | `Surface::Size` | $type.label.idle.size.Alt1 | Text.qss:92, :84, :100, :115, :123 |  |
| `Label::MarginV` | `$label.MarginV` | `label.MarginV` | Advanced | Margin above and below | 1 |  | $space.label.idle.margin-top.Alt1 $space.label.idle.margin-bottom.Alt1 | Text.qss:93-94 |  |

## The entry field

BaseStyleSheet.qss's rule of the entry controls (QTextEdit, QPlainTextEdit, QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox), and what LineEdit.qss, ComboBox.qss and SpinBox.qss share.

| Token | Root name | Key | Level | Label | Value | Follows | Purpose ids | Source | Note |
|---|---|---|---|---|---|---|---|---|---|
| `Field::Fill` | `$field.Fill` | `field.Fill` | Basic | Fill | #CCCCCC |  | $fill.field.idle.Alt1 | BaseStyleSheet.qss:76, ComboBox.qss:19 |  |
| `Field::Text` | `$field.Text` | `field.Text` | Basic | Text | #000000 | `Text::OnLight` | $text.field.idle.Alt1 | BaseStyleSheet.qss:77 |  |
| `Field::Glyph` | `$field.Glyph` | `field.Glyph` | Advanced | Arrows and marks | #000000 | `Field::Text` | $asset.arrow.idle.color | combobox-arrow.svg:9 | The arrows and marks drawn inside a field (the arrow of a combo box, the search and clear marks of a line edit, the arrows of a spin box). Each is an image with a colour of its own (combobox-arrow.svg:9 is #000000), and is drawn as it is while this has its O3DE Original value; once a theme changes it, the images are drawn in this colour (ThemedIcon). It follows the field's text. |
| `Field::Family` | `$field.Family` | `field.Family` | Advanced | Font family | "Open Sans" | `Surface::Family` | $type.field.idle.family | BaseStyleSheet.qss:78 |  |
| `Field::Radius` | `$field.Radius` | `field.Radius` | Basic | Corner radius | 2 |  | $radius.field.idle.all | BaseStyleSheet.qss:79 |  |
| `Field::Line` | `$field.Line` | `field.Line` | Standard | Line | #E9E9E9 |  | $line.field.idle.color.Alt1 | BaseStyleSheet.qss:81 |  |
| `Field::Size` | `$field.Size` | `field.Size` | Advanced | Font size | 12 | `Surface::Size` | $type.field.idle.size.Alt1 | BaseStyleSheet.qss:82 |  |
| `Field::Height` | `$field.Height` | `field.Height` | Standard | Height | 16 |  | $size.field.idle.height.Alt1 | LineEdit.qss:13, ComboBox.qss:15, SpinBox.qss:12, BrowseEdit.qss:17 |  |
| `Field::Margin` | `$field.Margin` | `field.Margin` | Advanced | Margin | 2, 2, 2, 2 |  | $space.field.idle.margin.Alt1 | SpinBox.qss:13, BrowseEdit.qss:12 |  |
| `Field::PaddingLeft` | `$field.PaddingLeft` | `field.PaddingLeft` | Advanced | Padding, left | 0 |  | $space.field.idle.padding-left.Alt1 | LineEdit.qss:15, SpinBox.qss:17 |  |
| `Field::PaddingTop` | `$field.PaddingTop` | `field.PaddingTop` | Advanced | Padding, top | -1 |  | $space.field.idle.padding-top.Alt1 | LineEdit.qss:19, SpinBox.qss:19 |  |
| `Field::PaddingBottom` | `$field.PaddingBottom` | `field.PaddingBottom` | Advanced | Padding, bottom | 1 |  | $space.field.idle.padding-bottom | LineEdit.qss:18, SpinBox.qss:18 |  |
| `Field::HoverLineWidth` | `$field.HoverLineWidth` | `field.HoverLineWidth` | Advanced | Hover line width | 1 |  | $line.field.hover.width.Alt1 | ComboBox.qss:25, SpinBox.qss:28, BaseStyleSheet.qss:102 |  |
| `Field::HoverMargin` | `$field.HoverMargin` | `field.HoverMargin` | Advanced | Hover margin | 1, 1, 1, 1 |  | $space.field.hover.margin.Alt1 | SpinBox.qss:27, BaseStyleSheet.qss:101 |  |
| `Field::HoverLine` | `$field.HoverLine` | `field.HoverLine` | Standard | Hover line | #222222 | `BG` | $line.field.hover.color.BG | SpinBox.qss:36, BaseStyleSheet.qss:104 | Fragment of BG: the line of a hovered field |
| `Field::HoverRadius` | `$field.HoverRadius` | `field.HoverRadius` | Standard | Hover corner radius | 3 |  | $radius.field.hover.all | ComboBox.qss:27, SpinBox.qss:30 |  |
| `Field::FocusLineWidth` | `$field.FocusLineWidth` | `field.FocusLineWidth` | Advanced | Focus line width | 1 |  | $line.field.focus.width.Alt1 | ComboBox.qss:36, SpinBox.qss:28, BaseStyleSheet.qss:112 |  |
| `Field::FocusMargin` | `$field.FocusMargin` | `field.FocusMargin` | Advanced | Focus margin | 1, 1, 1, 1 |  | $space.field.focus.margin.Alt1 | SpinBox.qss:27, BaseStyleSheet.qss:111 |  |
| `Field::FocusRadius` | `$field.FocusRadius` | `field.FocusRadius` | Standard | Focus corner radius | 3 |  | $radius.field.focus.all | ComboBox.qss:38, SpinBox.qss:30 |  |
| `Field::FocusLine` | `$field.FocusLine` | `field.FocusLine` | Standard | Focus line | #94D2FF | `Accent::Focus` | $line.field.focus.color.Alt1 | ComboBox.qss:39, SpinBox.qss:45, BaseStyleSheet.qss:113 |  |
| `Field::FocusFill` | `$field.FocusFill` | `field.FocusFill` | Standard | Focus fill | #FFFFFF |  | $fill.field.focus.Alt1 | ComboBox.qss:41, SpinBox.qss:46, BrowseEdit.qss:41 |  |
| `Field::ErrorMargin` | `$field.ErrorMargin` | `field.ErrorMargin` | Advanced | Error margin | 0, 0, 0, 0 |  | $space.field.HasError | ComboBox.qss:46, SpinBox.qss:62 |  |
| `Field::ErrorLineWidth` | `$field.ErrorLineWidth` | `field.ErrorLineWidth` | Advanced | Error line width | 2 |  | $line.field.HasError.width | ComboBox.qss:47, SpinBox.qss:63 |  |
| `Field::ErrorRadius` | `$field.ErrorRadius` | `field.ErrorRadius` | Standard | Error corner radius | 4 |  | $radius.field.HasError | ComboBox.qss:49, SpinBox.qss:65 |  |
| `Field::ErrorLine` | `$field.ErrorLine` | `field.ErrorLine` | Standard | Error line | #E25243 | `Accent::Error` | $line.field.HasError.color | ComboBox.qss:50, SpinBox.qss:66 |  |
| `Field::DisabledFill` | `$field.DisabledFill` | `field.DisabledFill` | Standard | Disabled fill | #666666 | `Fill::Disabled` | $fill.field.disabled | LineEdit.qss:33, ComboBox.qss:55, SpinBox.qss:72 |  |
| `Field::DisabledText` | `$field.DisabledText` | `field.DisabledText` | Standard | Disabled text | #999999 | `Text::Disabled` | $text.field.disabled.Alt1 | LineEdit.qss:34, ComboBox.qss:56, SpinBox.qss:73 |  |

## The secondary tab

TabWidget.qss's `AzQtComponents--TabBar.Secondary`, which TabElement draws and Script Canvas's graph tabs restate.

| Token | Root name | Key | Level | Label | Value | Follows | Purpose ids | Source | Note |
|---|---|---|---|---|---|---|---|---|---|
| `Tab::SecondaryText` | `$tab.SecondaryText` | `tab.SecondaryText` | Standard | Idle tab text | #959595 |  | $text.tab.idle | TabWidget.qss:142 |  |
| `Tab::SecondaryHoverFill` | `$tab.SecondaryHoverFill` | `tab.SecondaryHoverFill` | Standard | Hovered tab | #FFFFFF26 |  | $fill.tab.hover.Alt1 | TabWidget.qss:147 |  |
| `Tab::SecondaryLine` | `$tab.SecondaryLine` | `tab.SecondaryLine` | Advanced | Idle tab underline | #333333 | `Low` | $line.tab.idle.color.Low | TabWidget.qss:140 | Fragment of Low: the underline of an idle tab |
| `Tab::Fill` | `$tab.Fill` | `tab.Fill` | Standard | Idle tab | #333333 | `Low` | $fill.tab.idle.Low | TabWidget.qss:60 | Fragment of Low: a tab that is not the selected one |
| `Tab::TopRadius` | `$tab.TopRadius` | `tab.TopRadius` | Basic | Top corner radius | 0 |  | $radius.tab.idle.top | The radius of a tab's top corners. No rule of the legacy sheets gives a tab one (TabWidget.qss:57-62): square. | Qt default |
| `Tab::Height` | `$tab.Height` | `tab.Height` | Standard | Tab bar height | 28 |  | $size.tab.idle.min-height.Alt1 | TabWidget.qss:51, TabWidgetActionToolBar.qss:4, :29 |  |
| `Tab::SecondaryHeight` | `$tab.SecondaryHeight` | `tab.SecondaryHeight` | Advanced | Tool tab bar height | 30 |  | $size.tab.idle.min-height.Alt2 | TabWidget.qss:122 |  |
| `Tab::Padding` | `$tab.Padding` | `tab.Padding` | Advanced | Tab padding | 9, 6, 21, 6 |  | $space.tab.idle.padding.Alt1 | TabWidget.qss:61 |  |
| `Tab::SecondaryPadding` | `$tab.SecondaryPadding` | `tab.SecondaryPadding` | Advanced | Tool tab padding | 14, 7, 14, 5 |  | $space.tab.idle.padding.Alt3 | TabWidget.qss:141 |  |
| `Tab::BorderedPadding` | `$tab.BorderedPadding` | `tab.BorderedPadding` | Advanced | Bordered tab padding | 14, 6, 14, 6 |  | $space.tab.idle.padding.Alt2 | TabWidget.qss:155 |  |

## The families

What one family of controls has of its own, which a theme sets apart from the palette. A value that equals a tier follows the tier until a theme gives it a value.

| Token | Root name | Key | Level | Label | Value | Follows | Purpose ids | Source | Note |
|---|---|---|---|---|---|---|---|---|---|
| `Window::TitleBarFill` | `$window.TitleBarFill` | `window.TitleBarFill` | Standard | Title bar | #111111 | `FarBG` | $fill.bar.idle.FarBG | TitleBar.qss:11 | Fragment of FarBG: the title bar of a window and of a group of docked tabs |
| `Window::StatusBarFill` | `$window.StatusBarFill` | `window.StatusBarFill` | Standard | Status bar | #444444 | `Mid` | $fill.bar.idle.Mid | StatusBar.cpp:36 (StatusBar::defaultConfig) | Fragment of Mid: the status bar |
| `Window::DockTitleFill` | `$window.DockTitleFill` | `window.DockTitleFill` | Standard | Dock title | #333333 | `Low` | $fill.title.idle | QDockWidget.qss:22 | Fragment of Low: the title of a dock widget |
| `Window::TitleText` | `$window.TitleText` | `window.TitleText` | Standard | Dock title text | #CCCCCC |  | $text.surface.idle.Alt2 | BaseStyleSheet.qss:64 |  |
| `Window::FrameLine` | `$window.FrameLine` | `window.FrameLine` | Advanced | Window frame | #212223 |  | $line.dock.idle.color.Alt2 | StyledDockWidget::drawFrame, WindowDecorationWrapper::drawFrame | The line round a dock widget and a decorated window StyledDockWidget::drawFrame, WindowDecorationWrapper::drawFrame |
| `Window::ButtonHoverFill` | `$window.ButtonHoverFill` | `window.ButtonHoverFill` | Advanced | Title button, hovered | #FFFFFF19 |  | $fill.button.hover.Alt1 | 25 (QCss::parseColorValue) TitleBar.qss:100 | The fill of a minimize or maximize button under the pointer: `rgba(255,255,255,0.1)`, which Qt reads as an alpha of 25 (QCss::parseColorValue) |
| `Window::CloseHoverFill` | `$window.CloseHoverFill` | `window.CloseHoverFill` | Advanced | Close button, hovered | #B80D1C |  | $fill.button.hover.fill.Alt3 | TitleBar.qss:104 |  |
| `Scroll::Handle` | `$scroll.Handle` | `scroll.Handle` | Standard | Handle | #FFFFFF66 |  | $fill.scroll-handle.idle.Alt1 | ScrollBar.qss:31 |  |
| `Scroll::HandleOnLight` | `$scroll.HandleOnLight` | `scroll.HandleOnLight` | Advanced | Handle over a light view | #888888A3 |  | $fill.scroll-handle.idle.Alt2 | ScrollBar.qss:38 |  |
| `Scroll::HoverLane` | `$scroll.HoverLane` | `scroll.HoverLane` | Advanced | Lane under the pointer | #555555 | `Highest` | $fill.scroll.hover.Highest $fill.surface.hover.Highest | ScrollBar.qss:79, :93 | Fragment of Highest: the lane of a scroll bar under the pointer, and the corner between two |
| `Scroll::HoverLaneOnLight` | `$scroll.HoverLaneOnLight` | `scroll.HoverLaneOnLight` | Advanced | Lane over a light view | #DCDCDC |  | $fill.scroll.hover.Alt1 $fill.surface.hover.Alt1 | ScrollBar.qss:86, :99 |  |
| `Scroll::HandleRadius` | `$scroll.HandleRadius` | `scroll.HandleRadius` | Advanced | Handle corner radius | 2 |  | $radius.scroll-handle.idle | ScrollBar.qss:30, :37 |  |
| `Menu::Fill` | `$menu.Fill` | `menu.Fill` | Standard | Fill | #222222 | `BG` | $fill.menu.idle | Menu.qss:12 | Fragment of BG: the surface of a popup menu |
| `Menu::SelectedFill` | `$menu.SelectedFill` | `menu.SelectedFill` | Standard | Selected item | #444444 | `Mid` | $fill.item.selected.Mid | Menu.qss:20 | Fragment of Mid: the item of a menu under the pointer |
| `Menu::Separator` | `$menu.Separator` | `menu.Separator` | Standard | Separator | #454545 |  | $line.separator.idle.color | Menu.qss:31 |  |
| `Menu::BarFill` | `$menu.BarFill` | `menu.BarFill` | Standard | Menu bar | #444444 | `Mid` |  | The fill of a menu bar. The bar draws none (the universal rule, BaseStyleSheet.qss:17): it shows its window, a middle surface (BaseStyleSheet.qss:65), and so follows that tier. The fill is drawn once a theme changes it. BaseStyleSheet.qss:17, :65 |  |
| `Menu::BarSelectedFill` | `$menu.BarSelectedFill` | `menu.BarSelectedFill` | Advanced | Menu bar item, hovered | #555555 | `Highest` | $fill.item.selected.Highest | MenuBar.qss:30 | Fragment of Highest: the item of a menu bar under the pointer |
| `Menu::BarPressedFill` | `$menu.BarPressedFill` | `menu.BarPressedFill` | Advanced | Menu bar item, open | #222222 | `Menu::Fill` | $fill.item.pressed | MenuBar.qss:35 | The item of a menu bar whose menu is open: the menu's own surface, so the two read as one |
| `View::HoverFill` | `$view.HoverFill` | `view.HoverFill` | Standard | Row under the pointer | #5A5A5A |  | $fill.item.hover.Alt1 | TableView.qss:59 |  |
| `View::DisabledText` | `$view.DisabledText` | `view.DisabledText` | Advanced | Disabled row text | #777777 |  | $text.item.disabled.Alt1 | TableView.qss:101 |  |
| `Card::HeaderFill` | `$card.HeaderFill` | `card.HeaderFill` | Standard | Header | #333333 | `Low` | $fill.card.idle.Low $fill.surface.idle.Low | Card.qss:129, :155 | Fragment of Low: the header of a card |
| `Card::Fill` | `$card.Fill` | `card.Fill` | Standard | Body | #555555 | `Highest` | $fill.surface.idle.Highest $fill.card.idle.Highest | Card.qss:23, :88, :162 | Fragment of Highest: the body of a card, and the header of a card inside a card |
| `Card::ModifiedText` | `$card.ModifiedText` | `card.ModifiedText` | Standard | Title of a modified card | #E57829 |  | $text.card.modified | Card.qss:174 |  |
| `Card::Radius` | `$card.Radius` | `card.Radius` | Basic | Corner radius | 2 |  | $radius.surface.idle.bottom-left $radius.card.selected | Card.qss:31-32, :105-106, :112, :156-157 | The corners of a card: the top of its header, the bottom of its body |
| `Slider::Groove` | `$slider.Groove` | `slider.Groove` | Standard | Groove | #999999 |  | $fill.slider-groove.idle | SliderConfig.ini:21 |  |
| `Slider::GrooveHover` | `$slider.GrooveHover` | `slider.GrooveHover` | Advanced | Groove under the pointer | #CCDFF5 |  | $fill.slider-groove.hover | SliderConfig.ini:22 |  |
| `Slider::HandleDisabled` | `$slider.HandleDisabled` | `slider.HandleDisabled` | Advanced | Disabled handle | #999999 |  | $fill.slider-handle.disabled | SliderConfig.ini:16 |  |
| `Slider::ProgressTrack` | `$slider.ProgressTrack` | `slider.ProgressTrack` | Standard | Progress bar track | #9A9A9A |  | $fill.progress.idle.Alt1 | ProgressBar.qss:14 |  |
| `Button::Radius` | `$button.Radius` | `button.Radius` | Basic | Corner radius | 4 |  | $radius.button.idle.frame | PushButtonConfig.ini:3 |  |
| `Button::SmallIconRadius` | `$button.SmallIconRadius` | `button.SmallIconRadius` | Advanced | Small icon button corner radius | 4 | `Button::Radius` | $radius.small-icon.idle.frame | PushButtonConfig.ini:8 |  |
| `Button::SegmentFill` | `$button.SegmentFill` | `button.SegmentFill` | Advanced | Segment control | #333333 | `Low` | $fill.button.idle.fill.Low | SegmentControl.qss:16 | Fragment of Low: a button of a segment control |
| `Button::SegmentCheckedFill` | `$button.SegmentCheckedFill` | `button.SegmentCheckedFill` | Advanced | Segment control, on | #555555 | `Highest` | $fill.button.checked | SegmentControl.qss:70 | Fragment of Highest: the checked button of a segment control |
| `Chip::Fill` | `$chip.Fill` | `chip.Fill` | Advanced | Fill | #2E2E2E |  | $fill.button.idle.fill.Alt1 | FilteredSearchWidget.qss:31, :73 |  |
| `Chip::Line` | `$chip.Line` | `chip.Line` | Advanced | Line | #808080 |  | $line.button.idle.color.Alt1 | FilteredSearchWidget.qss:30, :72 |  |
| `Chip::Radius` | `$chip.Radius` | `chip.Radius` | Advanced | Corner radius | 2 |  | $radius.button.idle.all.Alt1 | FilteredSearchWidget.qss:29, :71 |  |

## The tools

What one tool has of its own. They are declared here, not in the tool's module, so that one catalogue lists every value a theme sets, whichever application shows the theme editor. A tool reads its tokens as any other.

| Token | Root name | Key | Level | Label | Value | Follows | Purpose ids | Source | Note |
|---|---|---|---|---|---|---|---|---|---|
| `Outliner::SelectedWash` | `$outliner.SelectedWash` | `outliner.SelectedWash` | Advanced | Selected row | #FFFFFF2D |  | $fill.view.selected.Alt3 | EntityOutlinerTreeView.hxx:99, EntityOutlinerListModel.hxx:373 | The wash over a selected row |
| `Outliner::HoverWash` | `$outliner.HoverWash` | `outliner.HoverWash` | Advanced | Row under the pointer | #FFFFFF1E |  | $fill.view.hover.Alt2 | EntityOutlinerTreeView.hxx:100, EntityOutlinerListModel.hxx:374 | The wash over the row the pointer is on |
| `Outliner::PrefabFill` | `$outliner.PrefabFill` | `outliner.PrefabFill` | Advanced | Prefab row | #444444 | `Mid` | $fill.item.idle.Mid | PrefabUiHandler.h:79 | Fragment of Mid: the row of a prefab |
| `Outliner::PrefabHoverFill` | `$outliner.PrefabHoverFill` | `outliner.PrefabHoverFill` | Advanced | Prefab row, hovered | #5A5A5A | `View::HoverFill` | $fill.item.hover.Alt3 | PrefabUiHandler.h:80 | The row of a prefab under the pointer: the colour of a view's row under the pointer |
| `Outliner::PrefabSelectedFill` | `$outliner.PrefabSelectedFill` | `outliner.PrefabSelectedFill` | Advanced | Prefab row, selected | #656565 | `Fill::ItemSelected` | $fill.item.selected.Alt3 | PrefabUiHandler.h:81 | The row of a selected prefab: the colour of a view's selected row |
| `Inspector::PaletteFill` | `$inspector.PaletteFill` | `inspector.PaletteFill` | Advanced | Add Component palette | #383A3B |  | $fill.surface.idle.Alt10 | Editor.qss:35 |  |
| `Inspector::PaletteLine` | `$inspector.PaletteLine` | `inspector.PaletteLine` | Advanced | Add Component palette, frame | #4E4E4E |  | $line.surface.idle.color.Alt5 | Editor.qss:34 |  |
| `Console::Fill` | `$console.Fill` | `console.Fill` | Advanced | Background | #222222 | `BG` | $fill.console.idle.BG | ConsoleSCB.cpp:458 (CConsoleSCB::RefreshStyle) | Fragment of BG: the background of the console |
| `Console::Text` | `$console.Text` | `console.Text` | Advanced | Text | #FFFFFF | `Text::Primary` | $text.console.idle.Alt2 | ConsoleSCB.cpp:450 (CConsoleSCB::RefreshStyle) | The text of the console: the colour of text |
| `Graph::Fill` | `$graph.Fill` | `graph.Fill` | Advanced | Canvas | #222222 | `BG` | $fill.graph.idle.BG | materialcanvas_style.json:5 | Fragment of BG: the canvas of a graph |
| `Graph::GridLine` | `$graph.GridLine` | `graph.GridLine` | Advanced | Grid lines | #282828 |  | $line.graph.idle.grid-major-color $line.graph.idle.grid-minor-color | materialcanvas_style.json:7, :11 | The lines of the grid, major and minor |
| `Graph::AnimationFill` | `$graph.AnimationFill` | `graph.AnimationFill` | Advanced | Animation graph canvas | #2F2F2F |  | $fill.node.idle.Alt16 | NodeGraphWidget.cpp:251 (NodeGraphWidget::paintEvent) | The canvas of an animation graph (the Animation Editor) |

## Fragments

A fragment equals a tier's value but serves another purpose, so it is a token of its own. It follows its tier until a
theme gives it a value: a scheme that moves the tiers moves it with them, and a theme may set it apart.

| Token | Value | Fragment of | Purpose |
|---|---|---|---|
| `Text::DisabledItem` | #555555 | `Highest` | the text of a disabled menu or popup item |
| `Fill::ButtonPressed` | #444444 | `Mid` | a button while it is pressed |
| `Fill::ButtonLow` | #666666 | `Disabled` | the bottom of a secondary button's gradient |
| `Line::Segment` | #222222 | `BG` | the line of a segment control |
| `Line::ColorPickerFrame` | #333333 | `Low` | the frames of the colour picker's grid, preview and swatches |
| `Field::HoverLine` | #222222 | `BG` | the line of a hovered field |
| `Tab::SecondaryLine` | #333333 | `Low` | the underline of an idle tab |
| `Tab::Fill` | #333333 | `Low` | a tab that is not the selected one |
| `Window::TitleBarFill` | #111111 | `FarBG` | the title bar of a window and of a group of docked tabs |
| `Window::StatusBarFill` | #444444 | `Mid` | the status bar |
| `Window::DockTitleFill` | #333333 | `Low` | the title of a dock widget |
| `Scroll::HoverLane` | #555555 | `Highest` | the lane of a scroll bar under the pointer, and the corner between two |
| `Menu::Fill` | #222222 | `BG` | the surface of a popup menu |
| `Menu::SelectedFill` | #444444 | `Mid` | the item of a menu under the pointer |
| `Menu::BarSelectedFill` | #555555 | `Highest` | the item of a menu bar under the pointer |
| `Card::HeaderFill` | #333333 | `Low` | the header of a card |
| `Card::Fill` | #555555 | `Highest` | the body of a card, and the header of a card inside a card |
| `Button::SegmentFill` | #333333 | `Low` | a button of a segment control |
| `Button::SegmentCheckedFill` | #555555 | `Highest` | the checked button of a segment control |
| `Outliner::PrefabFill` | #444444 | `Mid` | the row of a prefab |
| `Console::Fill` | #222222 | `BG` | the background of the console |
| `Graph::Fill` | #222222 | `BG` | the canvas of a graph |

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
