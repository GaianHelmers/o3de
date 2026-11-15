/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#pragma once

#if !defined(Q_MOC_RUN)
#include <AzQtComponents/Components/Theme/ThemeToken.h>
#include <QColor>
#include <QMargins>
#endif

namespace AzQtComponents
{
    //! The foundations of the flattened GUI: the values many families stand on, each defined once. A rule of the legacy sheets that
    //! reached many families lives here, and a family uses it from here; a family's own value that sits on a primitive layer keeps
    //! its own name and token id and takes its value from the layer. A theme that shifts a foundation reaches every feature on it.
    //!
    //! Tokens. Each value here is a token (ThemeToken.h): the value written is the O3DE Original theme's, what the legacy sheets
    //! granted, and a theme may give it another while the application runs. A declaration reads
    //!   inline ThemeColor Name{ value or the token it follows, "key", level, "label"[, "where it shows"] };
    //! where the key is the token's root name without the `$`, the level says how far into the theme editor it shows (Basic,
    //! Standard, Advanced), the label is its name there, and the sentence after it, on a line of its own, is its tool tip. A
    //! token that follows another takes that token's value until a theme gives it one of its own. Read a token where a widget
    //! is polished or painted, name it with a reference (`const QColor& ToolBarFill = Foundations::Mid;`), and build what is
    //! made from tokens as a Themed value.
    //!
    //! Ids. A token's root name mirrors its path here: $tier.<Name> for a tier, $<namespace>.<Name> for the rest (Text::Primary is
    //! $text.Primary, Field::HoverLine is $field.HoverLine). Its purpose ids name what it is for, as the legacy sheets granted it:
    //! $<channel>.<role>.<state>[.<attribute>][.<tier> | .Clear | .AltN], where
    //!   channel    is fill, text, line, type, space, size, radius, asset or other;
    //!   role       is the widget the rule styles (button, label, field, tab, surface, view, item ...), a named variant of it joined
    //!              by a hyphen (lock-toggle, tab-pane);
    //!   state      is idle, a pseudo-state (hover, pressed, focus, checked, selected, disabled), or the variant a selector tests
    //!              as the sheet writes it (primary, drawSimple, HasError), joined to a pseudo-state by + (hover+primary);
    //!   attribute  is the sheet's property (min-height, margin-top), a Qt property in lower case (iconsize, minimumsize), or a
    //!              hyphenated name for a value Qt computes (box-pad, inner-contrast);
    //!   the last   part is the tier the value sits on, Clear, or AltN for the n-th value of one purpose.
    namespace Foundations
    {
        using enum ThemeLevel;

        // ------------------------------------------------------------------------------
        // Primitive layers: the background tiers of the BJDS grey ladder, darkest first, and no paint
        // ------------------------------------------------------------------------------

        inline const ThemeGroup TierGroup{ "tier", "Backgrounds", "The ladder of greys every surface stands on, darkest first" };

        // Each tier is the grey that many rules of the legacy sheets name; the lines cited are two of those rules

        // BaseStyleSheet.qss:52, TitleBar.qss:11 BJDS Gray Far BG $tier.FarBG
        inline ThemeColor FarBG{ QColor(0x11, 0x11, 0x11), "tier.FarBG", Basic, "Farthest background",
        "Title bars and the lines between docked panes: the darkest edges" };
        // Splitter.qss:10, Menu.qss:12 BJDS Gray BG $tier.BG
        inline ThemeColor BG{ QColor(0x22, 0x22, 0x22), "tier.BG", Basic, "Background",
        "Menus, the handles between panes, the console and the canvas of a graph" };
        // QDockWidget.qss:22, Card.qss:129 BJDS Gray Low $tier.Low
        inline ThemeColor Low{ QColor(0x33, 0x33, 0x33), "tier.Low", Basic, "Low surface",
        "Card headers, dock titles and the tabs that are not selected" };
        // BaseStyleSheet.qss:29, :65 BJDS Gray Mid $tier.Mid
        inline ThemeColor Mid{ QColor(0x44, 0x44, 0x44), "tier.Mid", Basic, "Middle surface",
        "Windows, panels, tool bars and the selected tab: the colour most of the interface has" };
        // Card.qss:23, MenuBar.qss:30 BJDS Gray Highest $tier.Highest
        inline ThemeColor Highest{ QColor(0x55, 0x55, 0x55), "tier.Highest", Basic, "Highest surface",
        "The body of a card, the menu bar item under the pointer, the lane of a scroll bar under the pointer" };
        //! No paint: a constant, not a token
        inline constexpr QColor Clear(0x00, 0x00, 0x00, 0x00);     // Qt::transparent $tier.Clear

        // ------------------------------------------------------------------------------
        // The palette: the colours several families share, each defined once. A family's or a role's value that is one of them
        // takes it from here, as the tiers above
        // ------------------------------------------------------------------------------

        namespace Text
        {
            inline const ThemeGroup Group{ "text", "Text", "The colours of text and of the marks drawn in a text's colour" };

            // BaseStyleSheet.qss:15, PushButton.qss:37 $text.Primary
            inline ThemeColor Primary{ QColor(0xFF, 0xFF, 0xFF), "text.Primary", Basic, "Text",
            "The text of every control that sets no colour of its own" };
            // PushButton.qss:43, LineEdit.qss:28, Card.qss:272 $text.Disabled
            inline ThemeColor Disabled{ QColor(0x99, 0x99, 0x99), "text.Disabled", Basic, "Disabled text",
            "The text of a control that is disabled" };
            // BaseStyleSheet.qss:31 (the entry fields) $text.OnLight
            inline ThemeColor OnLight{ QColor(0x00, 0x00, 0x00), "text.OnLight", Standard, "Text on a light fill" };
            // Text.qss:16 $text.Muted
            inline ThemeColor Muted{ QColor(0x88, 0x88, 0x88), "text.Muted", Standard, "Muted text" };
            // BaseStyleSheet.qss:91 (the text edits) $text.TextArea $text.field.idle.Alt2
            inline ThemeColor TextArea{ QColor(0x54, 0x54, 0x54), "text.TextArea", Standard, "Text area text" };
            // Text.qss:21-24 (`.highlightedText`) $text.Highlighted $text.label.idle.Alt6
            inline ThemeColor Highlighted{ QColor(0x44, 0xB2, 0xF8), "text.Highlighted", Standard, "Highlighted text" };
            //! Fragment of Highest: the text of a disabled menu or popup item
            //! Menu.qss:25, ComboBox.qss:110 $text.DisabledItem $text.item.disabled.Highest
            inline ThemeColor DisabledItem{ Foundations::Highest, "text.DisabledItem", Advanced, "Disabled menu item" };
            //! The text and the mark over a disabled fill, lighter than Disabled: a disabled primary button's text, a small icon
            //! button's disabled arrow, the knob and the dot of a disabled indicator that is on
            //! (toggleswitch/checked-disabled.svg:9, radiobutton/checked-disabled.svg:8) $text.button.disabled.Alt2
            //! $text.button.default+disabled $text.small-icon.disabled.arrow $fill.check-indicator.checked+disabled.mark
            //! PushButton.qss:63, PushButtonConfig.ini:11 $text.DisabledOnFill
            inline ThemeColor DisabledOnFill{ QColor(0xBB, 0xBB, 0xBB), "text.DisabledOnFill", Advanced, "Text on a disabled fill" };
        } // namespace Text

        namespace Accent
        {
            inline const ThemeGroup Group{ "accent", "Accents", "The colours that mark what is active, focused, selected or wrong" };

            //! BaseStyleSheet.qss:55, Splitter.qss:21, ProgressBar.qss:21 $accent.Primary
            inline ThemeColor Primary{ QColor(0x1E, 0x70, 0xEB), "accent.Primary", Basic, "Accent",
            "Checked boxes and switches, slider handles, progress, the primary button, a pane handle under the pointer" };
            // BaseStyleSheet.qss:107, SpinBox.qss:42, ComboBox.qss:31 $accent.Focus
            inline ThemeColor Focus{ QColor(0x94, 0xD2, 0xFF), "accent.Focus", Basic, "Focus and links",
            "The line round the field that has the focus, links, the frame of the selected card" };
            // TabWidget.qss:158 $accent.Selected
            inline ThemeColor Selected{ QColor(0x00, 0x8D, 0xE5), "accent.Selected", Standard, "Selected tab" };
            // SpinBox.qss:57, ComboBox.qss:44 $accent.Error
            inline ThemeColor Error{ QColor(0xE2, 0x52, 0x43), "accent.Error", Standard, "Error" };
            // VectorInput.qss:47 $accent.Info
            inline ThemeColor Info{ QColor(0x42, 0x85, 0xF4), "accent.Info", Standard, "Information" };
            // VectorInput.qss:42 $accent.Success
            inline ThemeColor Success{ QColor(0x58, 0xBC, 0x61), "accent.Success", Standard, "Success" };
            //! The light top of a primary button's gradient: PushButtonConfig.ini:22, ProjectManager.qss:17 $accent.PrimaryLight
            //! $fill.button.primary
            inline ThemeColor PrimaryLight{ QColor(0x00, 0x95, 0xF2), "accent.PrimaryLight", Advanced, "Primary button, top" };
            //! The light top of a hovered primary button's gradient
            //! PushButtonConfig.ini:20 $accent.PrimaryHoverLight $fill.button.hover+primary.Alt2
            inline ThemeColor PrimaryHoverLight{ QColor(0x4A, 0xBA, 0xFF), "accent.PrimaryHoverLight", Advanced, "Primary hover, top" };
            //! The body of a disabled primary control: a disabled primary button's gradient, a disabled indicator that is on
            //! (toggleswitch/checked-disabled.svg:8, radiobutton/checked-disabled.svg:7) $fill.button.disabled+primary.Alt2
            //! $fill.check-indicator.checked+disabled
            //! PushButtonConfig.ini:16-17 $accent.PrimaryDisabled
            inline ThemeColor PrimaryDisabled{ QColor(0x2A, 0x48, 0x75), "accent.PrimaryDisabled", Advanced, "Disabled accent" };
            //! The focus ring of a control: a focused push button's border, the ring of a focused indicator, on or off
            //! (toggleswitch/unchecked-focus.svg:8, radiobutton/unchecked-focus.svg:7) $line.button.focus.color.Alt2
            //! $line.check-indicator.focus.color
            //! PushButtonConfig.ini:45 $accent.FocusRing
            inline ThemeColor FocusRing{ QColor(0x4B, 0x8C, 0xEF), "accent.FocusRing", Standard, "Focus ring" };
        } // namespace Accent

        namespace Fill
        {
            inline const ThemeGroup Group{ "fill", "Fills", "The fills several families share that sit on no tier" };

            // LineEdit.qss:28, SpinBox.qss:69, ComboBox.qss:53 $fill.Disabled
            inline ThemeColor Disabled{ QColor(0x66, 0x66, 0x66), "fill.Disabled", Standard, "Disabled control" };
            // TableView.qss:42, :14 $fill.ItemSelected
            inline ThemeColor ItemSelected{ QColor(0x65, 0x65, 0x65), "fill.ItemSelected", Standard, "Selected row" };
            // TableView.qss:62 $fill.ItemHeader
            inline ThemeColor ItemHeader{ QColor(0x2D, 0x2D, 0x2D), "fill.ItemHeader", Standard, "View header" };
            // TableView.qss:14 $fill.ItemView
            inline ThemeColor ItemView{ QColor(0x4D, 0x4D, 0x4D), "fill.ItemView", Standard, "Alternate row" };
            // BaseStyleSheet.qss:90, AssetsTab.qss:38 $fill.FieldLight
            inline ThemeColor FieldLight{ QColor(0xE9, 0xE9, 0xE9), "fill.FieldLight", Standard, "Text area" };
            // TabWidget.qss:70 $fill.HoverOverlay
            inline ThemeColor HoverOverlay{ QColor(0xFF, 0xFF, 0xFF, 0x33), "fill.HoverOverlay", Standard, "Hover overlay" };
            // ToolTip.qss:13, ProjectManager.qss:462 $fill.Deepest
            inline ThemeColor Deepest{ QColor(0x00, 0x00, 0x00), "fill.Deepest", Standard, "Tool tip" };
            //! The light grey of a button's gradient: PushButtonConfig.ini:31-32, BrowseEdit.qss:77, ProjectManager.qss:29, :44
            //! $fill.ButtonLight $fill.button.hover.gradient $fill.button.disabled+primary $fill.button.pressed+secondary
            inline ThemeColor ButtonLight{ QColor(0x77, 0x77, 0x77), "fill.ButtonLight", Advanced, "Button, light" };
            //! The lighter grey a button's gradient starts from: PushButtonConfig.ini:30, ProjectManager.qss:35 $fill.ButtonHighlight
            //! $fill.button.secondary
            inline ThemeColor ButtonHighlight{ QColor(0x88, 0x88, 0x88), "fill.ButtonHighlight", Advanced, "Button, highlight" };
            //! Fragment of Mid: a button while it is pressed
            //! PushButtonConfig.ini:28-29 $fill.ButtonPressed $fill.button.pressed.Mid
            inline ThemeColor ButtonPressed{ Foundations::Mid, "fill.ButtonPressed", Advanced, "Button, pressed" };
            //! Fragment of Disabled: the bottom of a secondary button's gradient
            //! PushButtonConfig.ini:33 $fill.ButtonLow $fill.button.secondary.gradient-end
            inline ThemeColor ButtonLow{ Disabled, "fill.ButtonLow", Advanced, "Button, low" };
        } // namespace Fill

        namespace Line
        {
            inline const ThemeGroup Group{ "line", "Lines", "The borders and separators several families share" };

            // ScrollBar.qss:27, WindowDecorationWrapper.qss:9 $line.Dark
            inline ThemeColor Dark{ QColor(0x00, 0x00, 0x00), "line.Dark", Standard, "Dark line" };
            // InspectorGroupHeaderWidget.cpp:26, AssetBundler.qss:34 $line.Frame
            inline ThemeColor Frame{ QColor(0x1B, 0x1B, 0x1B), "line.Frame", Standard, "Frame line" };
            // TabWidget.qss:111 $line.TabPane
            inline ThemeColor TabPane{ QColor(0x2B, 0x2B, 0x2B), "line.TabPane", Advanced, "Tab pane line" };
            //! The white outline: ProjectManager.qss:631, :687, AssetFolderThumbnailViewConfig.ini:26, SliderConfig.ini:11 $line.Light
            //! $line.button.idle.color.Alt2 $line.thumbnail-child.selected.color $line.gradient-slider-handle.idle.color
            inline ThemeColor Light{ QColor(0xFF, 0xFF, 0xFF), "line.Light", Standard, "Light outline" };
            //! Fragment of BG: the line of a segment control
            //! SegmentControl.qss:17 $line.Segment $line.button.idle.color.BG
            inline ThemeColor Segment{ Foundations::BG, "line.Segment", Advanced, "Segment control line" };
            //! Fragment of Low: the frames of the colour picker's grid, preview and swatches
            //! ColorPicker.qss:108 $line.ColorPickerFrame $line.surface.idle.color.Low
            inline ThemeColor ColorPickerFrame{ Foundations::Low, "line.ColorPickerFrame", Advanced, "Color picker frames" };
        } // namespace Line

        // ------------------------------------------------------------------------------
        // The universal rule: BaseStyleSheet.qss's `*`, which reached every widget
        // ------------------------------------------------------------------------------

        namespace Surface
        {
            inline const ThemeGroup Group{ "surface", "Every widget", "What a widget has that no rule of its own changes" };

            inline constexpr QColor Fill = Clear;                  // BaseStyleSheet.qss:17 $surface.Fill $fill.surface.idle.Clear
            // BaseStyleSheet.qss:18 $surface.Text $text.surface.idle.Alt1
            inline ThemeColor Text{ Foundations::Text::Primary, "surface.Text", Advanced, "Text" };
            // BaseStyleSheet.qss:19 $surface.Family $type.surface.idle.family
            inline ThemeText Family{ "Open Sans", "surface.Family", Standard, "Font family" };
            // BaseStyleSheet.qss:20 $surface.Weight $type.surface.idle.weight
            inline ThemeInt Weight{ 400, "surface.Weight", Advanced, "Font weight" };
            //! StyleManager::initialize (the application font), ColorPicker.qss:18 (`ColorPicker *`), Text.qss:92 (`.Label`) $surface.Size
            //! $type.surface.idle.size
            inline ThemeInt Size{ 12, "surface.Size", Standard, "Font size" };
        } // namespace Surface

        // ------------------------------------------------------------------------------
        // The label rule: Text.qss's `.Label, QLabel`; the tiers of Text.qss and TitleBar.qss restate its family and size
        // ------------------------------------------------------------------------------

        namespace Label
        {
            inline const ThemeGroup Group{ "label", "Labels", "The text of a label" };

            //! Text.qss:91 (QLabel), :57, :66, :74, :83, :99, :114, :122 (the tiers), TitleBar.qss:33 $label.Family
            //! $type.label.idle.family.Alt1
            inline ThemeText Family{ Surface::Family, "label.Family", Advanced, "Font family" };
            //! Text.qss:92, :84, :100, :115, :123 $label.Size $type.label.idle.size.Alt1
            inline ThemeInt Size{ Surface::Size, "label.Size", Advanced, "Font size" };
            //! Text.qss:93-94 $label.MarginV $space.label.idle.margin-top.Alt1 $space.label.idle.margin-bottom.Alt1
            inline ThemeInt MarginV{ 1, "label.MarginV", Advanced, "Margin above and below" };
        } // namespace Label

        // ------------------------------------------------------------------------------
        // The entry field: BaseStyleSheet.qss's rule of the entry controls (QTextEdit, QPlainTextEdit, QLineEdit, QSpinBox,
        // QDoubleSpinBox, QComboBox), and what LineEdit.qss, ComboBox.qss and SpinBox.qss share
        // ------------------------------------------------------------------------------

        namespace Field
        {
            inline const ThemeGroup Group{ "field", "Entry fields", "Line edits, spin boxes and combo boxes" };

            //! BaseStyleSheet.qss:76, ComboBox.qss:19 $field.Fill $fill.field.idle.Alt1
            inline ThemeColor Fill{ QColor(0xCC, 0xCC, 0xCC), "field.Fill", Basic, "Fill",
            "The face of a line edit, a spin box and a combo box" };
            // BaseStyleSheet.qss:77 $field.Text $text.field.idle.Alt1
            inline ThemeColor Text{ Foundations::Text::OnLight, "field.Text", Basic, "Text",
            "The text typed into a field" };
            //! The arrows and marks drawn inside a field (the arrow of a combo box, the search and clear marks of a line edit, the
            //! arrows of a spin box). Each is an image with a colour of its own (combobox-arrow.svg:9 is #000000), and is drawn
            //! as it is while this has its O3DE Original value; once a theme changes it, the images are drawn in this colour
            //! (ThemedIcon). It follows the field's text.
            //! combobox-arrow.svg:9 $field.Glyph $asset.arrow.idle.color
            inline ThemeColor Glyph{ Text, "field.Glyph", Advanced, "Arrows and marks" };
            // BaseStyleSheet.qss:78 $field.Family $type.field.idle.family
            inline ThemeText Family{ Surface::Family, "field.Family", Advanced, "Font family" };
            // BaseStyleSheet.qss:79 $field.Radius $radius.field.idle.all
            inline ThemeInt Radius{ 2, "field.Radius", Basic, "Corner radius",
            "The corners of a field at rest" };
            // BaseStyleSheet.qss:81 $field.Line $line.field.idle.color.Alt1
            inline ThemeColor Line{ QColor(0xE9, 0xE9, 0xE9), "field.Line", Standard, "Line" };
            // BaseStyleSheet.qss:82 $field.Size $type.field.idle.size.Alt1
            inline ThemeInt Size{ Surface::Size, "field.Size", Advanced, "Font size" };
            //! LineEdit.qss:13, ComboBox.qss:15, SpinBox.qss:12, BrowseEdit.qss:17 $field.Height $size.field.idle.height.Alt1
            inline ThemeInt Height{ 16, "field.Height", Standard, "Height" };
            //! SpinBox.qss:13, BrowseEdit.qss:12 $field.Margin $space.field.idle.margin.Alt1
            inline ThemeMargins Margin{ QMargins(2, 2, 2, 2), "field.Margin", Advanced, "Margin" };
            //! LineEdit.qss:15, SpinBox.qss:17 $field.PaddingLeft $space.field.idle.padding-left.Alt1
            inline ThemeInt PaddingLeft{ 0, "field.PaddingLeft", Advanced, "Padding, left" };
            //! LineEdit.qss:19, SpinBox.qss:19 $field.PaddingTop $space.field.idle.padding-top.Alt1
            inline ThemeInt PaddingTop{ -1, "field.PaddingTop", Advanced, "Padding, top" };
            //! LineEdit.qss:18, SpinBox.qss:18 $field.PaddingBottom $space.field.idle.padding-bottom
            inline ThemeInt PaddingBottom{ 1, "field.PaddingBottom", Advanced, "Padding, bottom" };

            // ---- Hovered, focused, in error, disabled
            //! ComboBox.qss:25, SpinBox.qss:28, BaseStyleSheet.qss:102 $field.HoverLineWidth $line.field.hover.width.Alt1
            inline ThemeInt HoverLineWidth{ 1, "field.HoverLineWidth", Advanced, "Hover line width" };
            //! SpinBox.qss:27, BaseStyleSheet.qss:101 $field.HoverMargin $space.field.hover.margin.Alt1
            inline ThemeMargins HoverMargin{ QMargins(1, 1, 1, 1), "field.HoverMargin", Advanced, "Hover margin" };
            //! Fragment of BG: the line of a hovered field
            //! SpinBox.qss:36, BaseStyleSheet.qss:104 $field.HoverLine $line.field.hover.color.BG
            inline ThemeColor HoverLine{ Foundations::BG, "field.HoverLine", Standard, "Hover line" };
            //! ComboBox.qss:27, SpinBox.qss:30 $field.HoverRadius $radius.field.hover.all
            inline ThemeInt HoverRadius{ 3, "field.HoverRadius", Standard, "Hover corner radius" };
            //! ComboBox.qss:36, SpinBox.qss:28, BaseStyleSheet.qss:112 $field.FocusLineWidth $line.field.focus.width.Alt1
            inline ThemeInt FocusLineWidth{ 1, "field.FocusLineWidth", Advanced, "Focus line width" };
            //! SpinBox.qss:27, BaseStyleSheet.qss:111 $field.FocusMargin $space.field.focus.margin.Alt1
            inline ThemeMargins FocusMargin{ QMargins(1, 1, 1, 1), "field.FocusMargin", Advanced, "Focus margin" };
            //! ComboBox.qss:38, SpinBox.qss:30 $field.FocusRadius $radius.field.focus.all
            inline ThemeInt FocusRadius{ 3, "field.FocusRadius", Standard, "Focus corner radius" };
            //! ComboBox.qss:39, SpinBox.qss:45, BaseStyleSheet.qss:113 $field.FocusLine $line.field.focus.color.Alt1
            inline ThemeColor FocusLine{ Accent::Focus, "field.FocusLine", Standard, "Focus line" };
            //! ComboBox.qss:41, SpinBox.qss:46, BrowseEdit.qss:41 $field.FocusFill $fill.field.focus.Alt1
            inline ThemeColor FocusFill{ QColor(0xFF, 0xFF, 0xFF), "field.FocusFill", Standard, "Focus fill" };
            //! ComboBox.qss:46, SpinBox.qss:62 $field.ErrorMargin $space.field.HasError
            inline ThemeMargins ErrorMargin{ QMargins(0, 0, 0, 0), "field.ErrorMargin", Advanced, "Error margin" };
            //! ComboBox.qss:47, SpinBox.qss:63 $field.ErrorLineWidth $line.field.HasError.width
            inline ThemeInt ErrorLineWidth{ 2, "field.ErrorLineWidth", Advanced, "Error line width" };
            //! ComboBox.qss:49, SpinBox.qss:65 $field.ErrorRadius $radius.field.HasError
            inline ThemeInt ErrorRadius{ 4, "field.ErrorRadius", Standard, "Error corner radius" };
            //! ComboBox.qss:50, SpinBox.qss:66 $field.ErrorLine $line.field.HasError.color
            inline ThemeColor ErrorLine{ Accent::Error, "field.ErrorLine", Standard, "Error line" };
            //! LineEdit.qss:33, ComboBox.qss:55, SpinBox.qss:72 $field.DisabledFill $fill.field.disabled
            inline ThemeColor DisabledFill{ Foundations::Fill::Disabled, "field.DisabledFill", Standard, "Disabled fill" };
            //! LineEdit.qss:34, ComboBox.qss:56, SpinBox.qss:73 $field.DisabledText $text.field.disabled.Alt1
            inline ThemeColor DisabledText{ Foundations::Text::Disabled, "field.DisabledText", Standard, "Disabled text" };
        } // namespace Field

        // ------------------------------------------------------------------------------
        // The secondary tab: TabWidget.qss's `AzQtComponents--TabBar.Secondary`, which TabElement draws and Script Canvas's
        // graph tabs restate
        // ------------------------------------------------------------------------------

        namespace Tab
        {
            inline const ThemeGroup Group{ "tab", "Tabs", "Tab bars and their tabs" };

            // TabWidget.qss:142 $tab.SecondaryText $text.tab.idle
            inline ThemeColor SecondaryText{ QColor(0x95, 0x95, 0x95), "tab.SecondaryText", Standard, "Idle tab text" };
            //! TabWidget.qss:147 $tab.SecondaryHoverFill $fill.tab.hover.Alt1
            inline ThemeColor SecondaryHoverFill{ QColor(0xFF, 0xFF, 0xFF, 0x26), "tab.SecondaryHoverFill", Standard, "Hovered tab" };
            //! Fragment of Low: the underline of an idle tab
            //! TabWidget.qss:140 $tab.SecondaryLine $line.tab.idle.color.Low
            inline ThemeColor SecondaryLine{ Foundations::Low, "tab.SecondaryLine", Advanced, "Idle tab underline" };
            //! Fragment of Low: a tab that is not the selected one
            //! TabWidget.qss:60 $tab.Fill $fill.tab.idle.Low
            inline ThemeColor Fill{ Foundations::Low, "tab.Fill", Standard, "Idle tab" };

            // ---- The shape and the box of a tab
            //! The radius of a tab's top corners. No rule of the legacy sheets gives a tab one (TabWidget.qss:57-62): square.
            //! Qt default $tab.TopRadius $radius.tab.idle.top
            inline ThemeInt TopRadius{ 0, "tab.TopRadius", Basic, "Top corner radius",
            "The top corners of a tab; 0 is square" };
            //! TabWidget.qss:51, TabWidgetActionToolBar.qss:4, :29 $tab.Height $size.tab.idle.min-height.Alt1
            inline ThemeInt Height{ 28, "tab.Height", Standard, "Tab bar height" };
            //! TabWidget.qss:122 $tab.SecondaryHeight $size.tab.idle.min-height.Alt2
            inline ThemeInt SecondaryHeight{ 30, "tab.SecondaryHeight", Advanced, "Tool tab bar height" };
            //! TabWidget.qss:61 $tab.Padding $space.tab.idle.padding.Alt1
            inline ThemeMargins Padding{ QMargins(9, 6, 21, 6), "tab.Padding", Advanced, "Tab padding" };
            //! TabWidget.qss:141 $tab.SecondaryPadding $space.tab.idle.padding.Alt3
            inline ThemeMargins SecondaryPadding{ QMargins(14, 7, 14, 5), "tab.SecondaryPadding", Advanced, "Tool tab padding" };
            //! TabWidget.qss:155 $tab.BorderedPadding $space.tab.idle.padding.Alt2
            inline ThemeMargins BorderedPadding{ QMargins(14, 6, 14, 6), "tab.BorderedPadding", Advanced, "Bordered tab padding" };
        } // namespace Tab

        // ------------------------------------------------------------------------------
        // The families: what one family of controls has of its own, which a theme sets apart from the palette. A value that equals
        // a tier follows the tier until a theme gives it a value
        // ------------------------------------------------------------------------------

        namespace Window
        {
            inline const ThemeGroup Group{ "window", "Windows and docks", "Title bars, dock titles and the frames of windows" };

            //! Fragment of FarBG: the title bar of a window and of a group of docked tabs
            //! TitleBar.qss:11 $window.TitleBarFill $fill.bar.idle.FarBG
            inline ThemeColor TitleBarFill{ Foundations::FarBG, "window.TitleBarFill", Standard, "Title bar" };
            //! Fragment of Mid: the status bar
            //! StatusBar.cpp:36 (StatusBar::defaultConfig) $window.StatusBarFill $fill.bar.idle.Mid
            inline ThemeColor StatusBarFill{ Foundations::Mid, "window.StatusBarFill", Standard, "Status bar" };
            //! Fragment of Low: the title of a dock widget
            //! QDockWidget.qss:22 $window.DockTitleFill $fill.title.idle
            inline ThemeColor DockTitleFill{ Foundations::Low, "window.DockTitleFill", Standard, "Dock title" };
            // BaseStyleSheet.qss:64 $window.TitleText $text.surface.idle.Alt2
            inline ThemeColor TitleText{ QColor(0xCC, 0xCC, 0xCC), "window.TitleText", Standard, "Dock title text" };
            //! The line round a dock widget and a decorated window
            //! StyledDockWidget::drawFrame, WindowDecorationWrapper::drawFrame $window.FrameLine $line.dock.idle.color.Alt2
            inline ThemeColor FrameLine{ QColor(33, 34, 35), "window.FrameLine", Advanced, "Window frame" };
            //! The fill of a minimize or maximize button under the pointer: `rgba(255,255,255,0.1)`, which Qt reads as an alpha of
            //! 25 (QCss::parseColorValue)
            //! TitleBar.qss:100 $window.ButtonHoverFill $fill.button.hover.Alt1
            inline ThemeColor ButtonHoverFill{ QColor(255, 255, 255, 25), "window.ButtonHoverFill", Advanced, "Title button, hovered" };
            // TitleBar.qss:104 $window.CloseHoverFill $fill.button.hover.fill.Alt3
            inline ThemeColor CloseHoverFill{ QColor(0xB8, 0x0D, 0x1C), "window.CloseHoverFill", Advanced, "Close button, hovered" };
        } // namespace Window

        namespace Scroll
        {
            inline const ThemeGroup Group{ "scroll", "Scroll bars", "The handle and the lane of a scroll bar" };

            // ScrollBar.qss:31 $scroll.Handle $fill.scroll-handle.idle.Alt1
            inline ThemeColor Handle{ QColor(255, 255, 255, 102), "scroll.Handle", Standard, "Handle" };
            // ScrollBar.qss:38 $scroll.HandleOnLight $fill.scroll-handle.idle.Alt2
            inline ThemeColor HandleOnLight{ QColor(136, 136, 136, 163), "scroll.HandleOnLight", Advanced, "Handle over a light view" };
            //! Fragment of Highest: the lane of a scroll bar under the pointer, and the corner between two
            //! ScrollBar.qss:79, :93 $scroll.HoverLane $fill.scroll.hover.Highest $fill.surface.hover.Highest
            inline ThemeColor HoverLane{ Foundations::Highest, "scroll.HoverLane", Advanced, "Lane under the pointer" };
            // ScrollBar.qss:86, :99 $scroll.HoverLaneOnLight $fill.scroll.hover.Alt1 $fill.surface.hover.Alt1
            inline ThemeColor HoverLaneOnLight{ QColor(0xDC, 0xDC, 0xDC), "scroll.HoverLaneOnLight", Advanced, "Lane over a light view" };
            // ScrollBar.qss:30, :37 $scroll.HandleRadius $radius.scroll-handle.idle
            inline ThemeInt HandleRadius{ 2, "scroll.HandleRadius", Advanced, "Handle corner radius" };
        } // namespace Scroll

        namespace Menu
        {
            inline const ThemeGroup Group{ "menu", "Menus", "Popup menus and the menu bar" };

            //! Fragment of BG: the surface of a popup menu
            //! Menu.qss:12 $menu.Fill $fill.menu.idle
            inline ThemeColor Fill{ Foundations::BG, "menu.Fill", Standard, "Fill" };
            //! Fragment of Mid: the item of a menu under the pointer
            //! Menu.qss:20 $menu.SelectedFill $fill.item.selected.Mid
            inline ThemeColor SelectedFill{ Foundations::Mid, "menu.SelectedFill", Standard, "Selected item" };
            // Menu.qss:31 $menu.Separator $line.separator.idle.color
            inline ThemeColor Separator{ QColor(0x45, 0x45, 0x45), "menu.Separator", Standard, "Separator" };
            //! The fill of a menu bar. The bar draws none (the universal rule, BaseStyleSheet.qss:17): it shows its window, a
            //! middle surface (BaseStyleSheet.qss:65), and so follows that tier. The fill is drawn once a theme changes it.
            //! BaseStyleSheet.qss:17, :65 $menu.BarFill
            inline ThemeColor BarFill{ Foundations::Mid, "menu.BarFill", Standard, "Menu bar" };
            //! Fragment of Highest: the item of a menu bar under the pointer
            //! MenuBar.qss:30 $menu.BarSelectedFill $fill.item.selected.Highest
            inline ThemeColor BarSelectedFill{ Foundations::Highest, "menu.BarSelectedFill", Advanced, "Menu bar item, hovered" };
            //! The item of a menu bar whose menu is open: the menu's own surface, so the two read as one
            //! MenuBar.qss:35 $menu.BarPressedFill $fill.item.pressed
            inline ThemeColor BarPressedFill{ Fill, "menu.BarPressedFill", Advanced, "Menu bar item, open" };
        } // namespace Menu

        namespace View
        {
            inline const ThemeGroup Group{ "view", "Lists, trees and tables", "The rows of an item view" };

            // TableView.qss:59 $view.HoverFill $fill.item.hover.Alt1
            inline ThemeColor HoverFill{ QColor(0x5A, 0x5A, 0x5A), "view.HoverFill", Standard, "Row under the pointer" };
            // TableView.qss:101 $view.DisabledText $text.item.disabled.Alt1
            inline ThemeColor DisabledText{ QColor(0x77, 0x77, 0x77), "view.DisabledText", Advanced, "Disabled row text" };
        } // namespace View

        namespace Card
        {
            inline const ThemeGroup Group{ "card", "Cards", "The cards of the Inspector and the property editors" };

            //! Fragment of Low: the header of a card
            //! Card.qss:129, :155 $card.HeaderFill $fill.card.idle.Low $fill.surface.idle.Low
            inline ThemeColor HeaderFill{ Foundations::Low, "card.HeaderFill", Standard, "Header" };
            //! Fragment of Highest: the body of a card, and the header of a card inside a card
            //! Card.qss:23, :88, :162 $card.Fill $fill.surface.idle.Highest $fill.card.idle.Highest
            inline ThemeColor Fill{ Foundations::Highest, "card.Fill", Standard, "Body" };
            // Card.qss:174 $card.ModifiedText $text.card.modified
            inline ThemeColor ModifiedText{ QColor(0xE5, 0x78, 0x29), "card.ModifiedText", Standard, "Title of a modified card" };
            //! The corners of a card: the top of its header, the bottom of its body
            //! Card.qss:31-32, :105-106, :112, :156-157 $card.Radius $radius.surface.idle.bottom-left $radius.card.selected
            inline ThemeInt Radius{ 2, "card.Radius", Basic, "Corner radius",
            "The top corners of a card's header and the bottom corners of its body" };
        } // namespace Card

        namespace Slider
        {
            inline const ThemeGroup Group{ "slider", "Sliders and progress", "Sliders and progress bars" };

            // SliderConfig.ini:21 $slider.Groove $fill.slider-groove.idle
            inline ThemeColor Groove{ QColor(0x99, 0x99, 0x99), "slider.Groove", Standard, "Groove" };
            // SliderConfig.ini:22 $slider.GrooveHover $fill.slider-groove.hover
            inline ThemeColor GrooveHover{ QColor(0xCC, 0xDF, 0xF5), "slider.GrooveHover", Advanced, "Groove under the pointer" };
            // SliderConfig.ini:16 $slider.HandleDisabled $fill.slider-handle.disabled
            inline ThemeColor HandleDisabled{ QColor(0x99, 0x99, 0x99), "slider.HandleDisabled", Advanced, "Disabled handle" };
            // ProgressBar.qss:14 $slider.ProgressTrack $fill.progress.idle.Alt1
            inline ThemeColor ProgressTrack{ QColor(0x9A, 0x9A, 0x9A), "slider.ProgressTrack", Standard, "Progress bar track" };
        } // namespace Slider

        namespace Button
        {
            inline const ThemeGroup Group{ "button", "Buttons", "Push buttons" };

            // PushButtonConfig.ini:3 $button.Radius $radius.button.idle.frame
            inline ThemeInt Radius{ 4, "button.Radius", Basic, "Corner radius",
            "The corners of a push button" };
            // PushButtonConfig.ini:8 $button.SmallIconRadius $radius.small-icon.idle.frame
            inline ThemeInt SmallIconRadius{ Radius, "button.SmallIconRadius", Advanced, "Small icon button corner radius" };
            //! Fragment of Low: a button of a segment control
            //! SegmentControl.qss:16 $button.SegmentFill $fill.button.idle.fill.Low
            inline ThemeColor SegmentFill{ Foundations::Low, "button.SegmentFill", Advanced, "Segment control" };
            //! Fragment of Highest: the checked button of a segment control
            //! SegmentControl.qss:70 $button.SegmentCheckedFill $fill.button.checked
            inline ThemeColor SegmentCheckedFill{ Foundations::Highest, "button.SegmentCheckedFill", Advanced, "Segment control, on" };
        } // namespace Button

        namespace Chip
        {
            inline const ThemeGroup Group{ "chip", "Filter chips", "The chips of a search field's filters" };

            // FilteredSearchWidget.qss:31, :73 $chip.Fill $fill.button.idle.fill.Alt1
            inline ThemeColor Fill{ QColor(46, 46, 46), "chip.Fill", Advanced, "Fill" };
            // FilteredSearchWidget.qss:30, :72 $chip.Line $line.button.idle.color.Alt1
            inline ThemeColor Line{ QColor(0x80, 0x80, 0x80), "chip.Line", Advanced, "Line" };
            // FilteredSearchWidget.qss:29, :71 $chip.Radius $radius.button.idle.all.Alt1
            inline ThemeInt Radius{ 2, "chip.Radius", Advanced, "Corner radius" };
        } // namespace Chip

        // ------------------------------------------------------------------------------
        // The tools: what one tool has of its own. They are declared here, not in the tool's module, so that one catalogue lists
        // every value a theme sets, whichever application shows the theme editor. A tool reads its tokens as any other.
        // ------------------------------------------------------------------------------

        namespace Outliner
        {
            inline const ThemeGroup Group{ "outliner", "Entity Outliner", "The rows of the Entity Outliner" };

            //! The wash over a selected row
            //! EntityOutlinerTreeView.hxx:99, EntityOutlinerListModel.hxx:373 $outliner.SelectedWash $fill.view.selected.Alt3
            inline ThemeColor SelectedWash{ QColor(255, 255, 255, 45), "outliner.SelectedWash", Advanced, "Selected row" };
            //! The wash over the row the pointer is on
            //! EntityOutlinerTreeView.hxx:100, EntityOutlinerListModel.hxx:374 $outliner.HoverWash $fill.view.hover.Alt2
            inline ThemeColor HoverWash{ QColor(255, 255, 255, 30), "outliner.HoverWash", Advanced, "Row under the pointer" };
            //! Fragment of Mid: the row of a prefab
            //! PrefabUiHandler.h:79 $outliner.PrefabFill $fill.item.idle.Mid
            inline ThemeColor PrefabFill{ Foundations::Mid, "outliner.PrefabFill", Advanced, "Prefab row" };
            //! The row of a prefab under the pointer: the colour of a view's row under the pointer
            //! PrefabUiHandler.h:80 $outliner.PrefabHoverFill $fill.item.hover.Alt3
            inline ThemeColor PrefabHoverFill{ View::HoverFill, "outliner.PrefabHoverFill", Advanced, "Prefab row, hovered" };
            //! The row of a selected prefab: the colour of a view's selected row
            //! PrefabUiHandler.h:81 $outliner.PrefabSelectedFill $fill.item.selected.Alt3
            inline ThemeColor PrefabSelectedFill{ Fill::ItemSelected, "outliner.PrefabSelectedFill", Advanced, "Prefab row, selected" };
        } // namespace Outliner

        namespace Inspector
        {
            inline const ThemeGroup Group{ "inspector", "Inspector", "The Add Component palette of the Inspector" };

            // Editor.qss:35 $inspector.PaletteFill $fill.surface.idle.Alt10
            inline ThemeColor PaletteFill{ QColor(0x38, 0x3A, 0x3B), "inspector.PaletteFill", Advanced, "Add Component palette" };
            // Editor.qss:34 $inspector.PaletteLine $line.surface.idle.color.Alt5
            inline ThemeColor PaletteLine{ QColor(0x4E, 0x4E, 0x4E), "inspector.PaletteLine", Advanced, "Add Component palette, frame" };
        } // namespace Inspector

        namespace Console
        {
            inline const ThemeGroup Group{ "console", "Console", "The Editor's console on its dark background" };

            //! Fragment of BG: the background of the console
            //! ConsoleSCB.cpp:458 (CConsoleSCB::RefreshStyle) $console.Fill $fill.console.idle.BG
            inline ThemeColor Fill{ Foundations::BG, "console.Fill", Advanced, "Background" };
            //! The text of the console: the colour of text
            //! ConsoleSCB.cpp:450 (CConsoleSCB::RefreshStyle) $console.Text $text.console.idle.Alt2
            inline ThemeColor Text{ Foundations::Text::Primary, "console.Text", Advanced, "Text" };
        } // namespace Console

        namespace Graph
        {
            inline const ThemeGroup Group{ "graph", "Node graphs", "The canvas of Script Canvas, Material Canvas and the other graphs" };

            //! Fragment of BG: the canvas of a graph
            //! materialcanvas_style.json:5 $graph.Fill $fill.graph.idle.BG
            inline ThemeColor Fill{ Foundations::BG, "graph.Fill", Advanced, "Canvas" };
            //! The lines of the grid, major and minor
            //! materialcanvas_style.json:7, :11 $graph.GridLine $line.graph.idle.grid-major-color $line.graph.idle.grid-minor-color
            inline ThemeColor GridLine{ QColor(0x28, 0x28, 0x28), "graph.GridLine", Advanced, "Grid lines" };
            //! The canvas of an animation graph (the Animation Editor)
            //! NodeGraphWidget.cpp:251 (NodeGraphWidget::paintEvent) $graph.AnimationFill $fill.node.idle.Alt16
            inline ThemeColor AnimationFill{ QColor(47, 47, 47), "graph.AnimationFill", Advanced, "Animation graph canvas" };
        } // namespace Graph

        // ------------------------------------------------------------------------------
        // Derived: the values an element or Qt computes from the tokens above. None is a constant of its own: each follows its base
        // token, as it did under the legacy sheets. An entry reads: id | from: its base | the formula | where it shows
        // ------------------------------------------------------------------------------

        // An element's shades (setShades in ElementCommon.cpp, as QRenderRule::configurePalette shades a rule's background; the factors
        // are tokens there):
        //   $line.shade.idle.light | from: the element's fill | fill.lighter(115) | Light: a sunken separator's light line, #4E4E4E on Mid
        //   $line.shade.idle.midlight | from: the element's fill | fill.lighter(107) | Midlight
        //   $line.shade.idle.dark | from: the element's fill | fill.darker(150) | Dark: the separator's dark line, #2D2D2D on Mid
        //   $line.shade.idle.shadow | from: the element's fill | fill.darker(300) | Shadow

        // Fusion's colours, where the base style draws over the palette an element gave the widget (QFusionStylePrivate::outline,
        // QFusionStylePrivate::tabFrameColor):
        //   $line.surface.idle.color.Alt6 | from: Window, a panel's fill (Mid) | outline.lighter(108), outline = Window.darker(140)
        //     (QFusionStylePrivate::outline) | a frame's line (QFusionStyle::drawPrimitive, PE_Frame), #343434
        //   $fill.tab-pane.idle.Alt2 | from: Button, the plain tab fill (Mid) | QFusionStylePrivate::tabFrameColor: buttonColor
        //     lightened by 4 percent, buttonColor = Button.lighter(100 + max(1, (180 - gray) / 6)) at three quarters of its
        //     saturation (QFusionStylePrivate::buttonColor) | a plain tab widget's pane (PE_FrameTabWidget), #535353
        //   $line.tab-pane.idle.color.Alt2 | from: Window (Mid) | outline = Window.darker(140) | that pane's border, #303030

        // Fusion's own shades, Qt constants drawn over the tokens (QFusionStylePrivate::lightShade, darkShade, topShadow,
        // innerContrastLine):
        //   $fill.bevel.idle.light-shade | from: none, a Qt constant | white at alpha 90 (lightShade) | a splitter's grip
        //     (QFusionStyle::drawControl, CE_Splitter)
        //   $fill.bevel.idle.dark-shade | from: none, a Qt constant | black at alpha 60 (darkShade) | the same grip
        //   $line.bevel.idle.top-shadow | from: none, a Qt constant | black at alpha 18 (topShadow) | Fusion's top shadows
        //   $line.bevel.idle.inner-contrast | from: none, a Qt constant | white at alpha 30 (innerContrastLine) | a pane's inner line,
        //     #676767 over #535353
        //   $line.bevel.idle.pane-shadow | from: none, a Qt constant | black at alpha 15 | the pane's bottom line (PE_FrameTabWidget)
    } // namespace Foundations
} // namespace AzQtComponents
