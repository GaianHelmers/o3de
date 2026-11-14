/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#pragma once

#if !defined(Q_MOC_RUN)
#include <QColor>
#include <QMargins>
#endif

namespace AzQtComponents
{
    //! The foundations of the flattened GUI: the values many families stand on, each defined once. A rule of the legacy sheets that
    //! reached many families lives here, and a family uses it from here; a family's own value that sits on a primitive layer keeps
    //! its own name and token id and takes its value from the layer. A theme that shifts a foundation reaches every feature on it.
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
        // ------------------------------------------------------------------------------
        // Primitive layers: the background tiers of the BJDS grey ladder, darkest first, and no paint
        // ------------------------------------------------------------------------------

        // Each tier is the grey that many rules of the legacy sheets name; the lines cited are two of those rules
        // BaseStyleSheet.qss:52, TitleBar.qss:11
        inline constexpr QColor FarBG(0x11, 0x11, 0x11);           // BJDS Gray Far BG $tier.FarBG
        // Splitter.qss:10, Menu.qss:12
        inline constexpr QColor BG(0x22, 0x22, 0x22);              // BJDS Gray BG $tier.BG
        // QDockWidget.qss:22, Card.qss:129
        inline constexpr QColor Low(0x33, 0x33, 0x33);             // BJDS Gray Low $tier.Low
        // BaseStyleSheet.qss:29, :65
        inline constexpr QColor Mid(0x44, 0x44, 0x44);             // BJDS Gray Mid $tier.Mid
        // Card.qss:23, MenuBar.qss:30
        inline constexpr QColor Highest(0x55, 0x55, 0x55);         // BJDS Gray Highest $tier.Highest
        inline constexpr QColor Clear(0x00, 0x00, 0x00, 0x00);     // Qt::transparent $tier.Clear

        // ------------------------------------------------------------------------------
        // The palette: the colours several families share, each defined once. A family's or a role's value that is one of them
        // takes it from here, as the tiers above
        // ------------------------------------------------------------------------------

        namespace Text
        {
            inline constexpr QColor Primary(0xFF, 0xFF, 0xFF);     // BaseStyleSheet.qss:15, PushButton.qss:37 $text.Primary
            inline constexpr QColor Disabled(0x99, 0x99, 0x99);    // PushButton.qss:43, LineEdit.qss:28, Card.qss:272 $text.Disabled
            inline constexpr QColor OnLight(0x00, 0x00, 0x00);     // BaseStyleSheet.qss:31 (the entry fields) $text.OnLight
            inline constexpr QColor Muted(0x88, 0x88, 0x88);       // Text.qss:16 $text.Muted
            //! Fragment of Highest: the text of a disabled menu or popup item
            //! Menu.qss:25, ComboBox.qss:110 $text.DisabledItem $text.item.disabled.Highest
            inline constexpr QColor DisabledItem(0x55, 0x55, 0x55);
            //! The text and the mark over a disabled fill, lighter than Disabled: a disabled primary button's text, a small icon
            //! button's disabled arrow, the knob and the dot of a disabled indicator that is on (toggleswitch/checked-disabled.svg:9,
            //! radiobutton/checked-disabled.svg:8) $text.button.disabled.Alt2 $text.button.default+disabled
            //! $text.small-icon.disabled.arrow $fill.check-indicator.checked+disabled.mark
            inline constexpr QColor DisabledOnFill(0xBB, 0xBB, 0xBB);  // PushButton.qss:63, PushButtonConfig.ini:11 $text.DisabledOnFill
        } // namespace Text

        namespace Accent
        {
            //! BaseStyleSheet.qss:55, Splitter.qss:21, ProgressBar.qss:21 $accent.Primary
            inline constexpr QColor Primary(0x1E, 0x70, 0xEB);
            inline constexpr QColor Focus(0x94, 0xD2, 0xFF);       // BaseStyleSheet.qss:107, SpinBox.qss:42, ComboBox.qss:31 $accent.Focus
            inline constexpr QColor Selected(0x00, 0x8D, 0xE5);    // TabWidget.qss:158 $accent.Selected
            inline constexpr QColor Error(0xE2, 0x52, 0x43);       // SpinBox.qss:57, ComboBox.qss:44 $accent.Error
            inline constexpr QColor Info(0x42, 0x85, 0xF4);        // VectorInput.qss:47 $accent.Info
            inline constexpr QColor Success(0x58, 0xBC, 0x61);     // VectorInput.qss:42 $accent.Success
            //! The light top of a primary button's gradient: PushButtonConfig.ini:22, ProjectManager.qss:17 $accent.PrimaryLight
            //! $fill.button.primary
            inline constexpr QColor PrimaryLight(0x00, 0x95, 0xF2);
            //! The body of a disabled primary control: a disabled primary button's gradient, a disabled indicator that is on
            //! (toggleswitch/checked-disabled.svg:8, radiobutton/checked-disabled.svg:7) $fill.button.disabled+primary.Alt2
            //! $fill.check-indicator.checked+disabled
            inline constexpr QColor PrimaryDisabled(0x2A, 0x48, 0x75); // PushButtonConfig.ini:16-17 $accent.PrimaryDisabled
            //! The focus ring of a control: a focused push button's border, the ring of a focused indicator, on or off
            //! (toggleswitch/unchecked-focus.svg:8, radiobutton/unchecked-focus.svg:7) $line.button.focus.color.Alt2
            //! $line.check-indicator.focus.color
            inline constexpr QColor FocusRing(0x4B, 0x8C, 0xEF);       // PushButtonConfig.ini:45 $accent.FocusRing
        } // namespace Accent

        namespace Fill
        {
            inline constexpr QColor Disabled(0x66, 0x66, 0x66);    // LineEdit.qss:28, SpinBox.qss:69, ComboBox.qss:53 $fill.Disabled
            inline constexpr QColor ItemSelected(0x65, 0x65, 0x65); // TableView.qss:42, :14 $fill.ItemSelected
            inline constexpr QColor ItemHeader(0x2D, 0x2D, 0x2D);  // TableView.qss:62 $fill.ItemHeader
            inline constexpr QColor ItemView(0x4D, 0x4D, 0x4D);    // TableView.qss:14 $fill.ItemView
            inline constexpr QColor FieldLight(0xE9, 0xE9, 0xE9);  // BaseStyleSheet.qss:90, AssetsTab.qss:38 $fill.FieldLight
            inline constexpr QColor HoverOverlay(0xFF, 0xFF, 0xFF, 0x33); // TabWidget.qss:70 $fill.HoverOverlay
            inline constexpr QColor Deepest(0x00, 0x00, 0x00);     // ToolTip.qss:13, ProjectManager.qss:462 $fill.Deepest
            //! The light grey of a button's gradient: PushButtonConfig.ini:31-32, BrowseEdit.qss:77, ProjectManager.qss:29, :44
            //! $fill.ButtonLight $fill.button.hover.gradient $fill.button.disabled+primary $fill.button.pressed+secondary
            inline constexpr QColor ButtonLight(0x77, 0x77, 0x77);
            //! The lighter grey a button's gradient starts from: PushButtonConfig.ini:30, ProjectManager.qss:35 $fill.ButtonHighlight
            //! $fill.button.secondary
            inline constexpr QColor ButtonHighlight(0x88, 0x88, 0x88);
        } // namespace Fill

        namespace Line
        {
            inline constexpr QColor Dark(0x00, 0x00, 0x00);        // ScrollBar.qss:27, WindowDecorationWrapper.qss:9 $line.Dark
            inline constexpr QColor Frame(0x1B, 0x1B, 0x1B);       // InspectorGroupHeaderWidget.cpp:26, AssetBundler.qss:34 $line.Frame
            inline constexpr QColor TabPane(0x2B, 0x2B, 0x2B);     // TabWidget.qss:111 $line.TabPane
            //! The white outline: ProjectManager.qss:631, :687, AssetFolderThumbnailViewConfig.ini:26, SliderConfig.ini:11 $line.Light
            //! $line.button.idle.color.Alt2 $line.thumbnail-child.selected.color $line.gradient-slider-handle.idle.color
            inline constexpr QColor Light(0xFF, 0xFF, 0xFF);
            //! Fragment of BG: the line of a segment control
            inline constexpr QColor Segment(0x22, 0x22, 0x22);     // SegmentControl.qss:17 $line.Segment $line.button.idle.color.BG
            //! Fragment of Low: the frames of the colour picker's grid, preview and swatches
            //! ColorPicker.qss:108 $line.ColorPickerFrame $line.surface.idle.color.Low
            inline constexpr QColor ColorPickerFrame(0x33, 0x33, 0x33);
        } // namespace Line

        // ------------------------------------------------------------------------------
        // The universal rule: BaseStyleSheet.qss's `*`, which reached every widget
        // ------------------------------------------------------------------------------

        namespace Surface
        {
            inline constexpr QColor Fill = Clear;                  // BaseStyleSheet.qss:17 $surface.Fill $fill.surface.idle.Clear
            inline constexpr QColor Text = Foundations::Text::Primary;  // BaseStyleSheet.qss:18 $surface.Text $text.surface.idle.Alt1
            inline constexpr const char* Family = "Open Sans";     // BaseStyleSheet.qss:19 $surface.Family $type.surface.idle.family
            inline constexpr int Weight = 400;                     // BaseStyleSheet.qss:20 $surface.Weight $type.surface.idle.weight
            //! StyleManager::initialize (the application font), ColorPicker.qss:18 (`ColorPicker *`), Text.qss:92 (`.Label`) $surface.Size
            //! $type.surface.idle.size
            inline constexpr int Size = 12;
        } // namespace Surface

        // ------------------------------------------------------------------------------
        // The label rule: Text.qss's `.Label, QLabel`; the tiers of Text.qss and TitleBar.qss restate its family and size
        // ------------------------------------------------------------------------------

        namespace Label
        {
            //! Text.qss:91 (QLabel), :57, :66, :74, :83, :99, :114, :122 (the tiers), TitleBar.qss:33 $label.Family
            //! $type.label.idle.family.Alt1
            inline constexpr const char* Family = "Open Sans";
            //! Text.qss:92, :84, :100, :115, :123 $label.Size $type.label.idle.size.Alt1
            inline constexpr int Size = 12;
            //! Text.qss:93-94 $label.MarginV $space.label.idle.margin-top.Alt1 $space.label.idle.margin-bottom.Alt1
            inline constexpr int MarginV = 1;
        } // namespace Label

        // ------------------------------------------------------------------------------
        // The entry field: BaseStyleSheet.qss's rule of the entry controls (QTextEdit, QPlainTextEdit, QLineEdit, QSpinBox,
        // QDoubleSpinBox, QComboBox), and what LineEdit.qss, ComboBox.qss and SpinBox.qss share
        // ------------------------------------------------------------------------------

        namespace Field
        {
            //! BaseStyleSheet.qss:76, ComboBox.qss:19 $field.Fill $fill.field.idle.Alt1
            inline constexpr QColor Fill(0xCC, 0xCC, 0xCC);
            inline constexpr QColor Text = Foundations::Text::OnLight;  // BaseStyleSheet.qss:77 $field.Text $text.field.idle.Alt1
            inline constexpr const char* Family = "Open Sans";     // BaseStyleSheet.qss:78 $field.Family $type.field.idle.family
            inline constexpr int Radius = 2;                       // BaseStyleSheet.qss:79 $field.Radius $radius.field.idle.all
            inline constexpr QColor Line(0xE9, 0xE9, 0xE9);        // BaseStyleSheet.qss:81 $field.Line $line.field.idle.color.Alt1
            inline constexpr int Size = 12;                        // BaseStyleSheet.qss:82 $field.Size $type.field.idle.size.Alt1
            //! LineEdit.qss:13, ComboBox.qss:15, SpinBox.qss:12, BrowseEdit.qss:17 $field.Height $size.field.idle.height.Alt1
            inline constexpr int Height = 16;
            //! SpinBox.qss:13, BrowseEdit.qss:12 $field.Margin $space.field.idle.margin.Alt1
            inline constexpr QMargins Margin(2, 2, 2, 2);
            //! LineEdit.qss:15, SpinBox.qss:17 $field.PaddingLeft $space.field.idle.padding-left.Alt1
            inline constexpr int PaddingLeft = 0;
            //! LineEdit.qss:19, SpinBox.qss:19 $field.PaddingTop $space.field.idle.padding-top.Alt1
            inline constexpr int PaddingTop = -1;
            //! LineEdit.qss:18, SpinBox.qss:18 $field.PaddingBottom $space.field.idle.padding-bottom
            inline constexpr int PaddingBottom = 1;

            // ---- Hovered, focused, in error, disabled
            //! ComboBox.qss:25, SpinBox.qss:28, BaseStyleSheet.qss:102 $field.HoverLineWidth $line.field.hover.width.Alt1
            inline constexpr int HoverLineWidth = 1;
            //! SpinBox.qss:27, BaseStyleSheet.qss:101 $field.HoverMargin $space.field.hover.margin.Alt1
            inline constexpr QMargins HoverMargin(1, 1, 1, 1);
            //! Fragment of BG: the line of a hovered field
            //! SpinBox.qss:36, BaseStyleSheet.qss:104 $field.HoverLine $line.field.hover.color.BG
            inline constexpr QColor HoverLine(0x22, 0x22, 0x22);
            //! ComboBox.qss:27, SpinBox.qss:30 $field.HoverRadius $radius.field.hover.all
            inline constexpr int HoverRadius = 3;
            //! ComboBox.qss:36, SpinBox.qss:28, BaseStyleSheet.qss:112 $field.FocusLineWidth $line.field.focus.width.Alt1
            inline constexpr int FocusLineWidth = 1;
            //! SpinBox.qss:27, BaseStyleSheet.qss:111 $field.FocusMargin $space.field.focus.margin.Alt1
            inline constexpr QMargins FocusMargin(1, 1, 1, 1);
            //! ComboBox.qss:38, SpinBox.qss:30 $field.FocusRadius $radius.field.focus.all
            inline constexpr int FocusRadius = 3;
            //! ComboBox.qss:39, SpinBox.qss:45, BaseStyleSheet.qss:113 $field.FocusLine $line.field.focus.color.Alt1
            inline constexpr QColor FocusLine = Accent::Focus;
            //! ComboBox.qss:41, SpinBox.qss:46, BrowseEdit.qss:41 $field.FocusFill $fill.field.focus.Alt1
            inline constexpr QColor FocusFill(0xFF, 0xFF, 0xFF);
            //! ComboBox.qss:46, SpinBox.qss:62 $field.ErrorMargin $space.field.HasError
            inline constexpr QMargins ErrorMargin(0, 0, 0, 0);
            //! ComboBox.qss:47, SpinBox.qss:63 $field.ErrorLineWidth $line.field.HasError.width
            inline constexpr int ErrorLineWidth = 2;
            //! ComboBox.qss:49, SpinBox.qss:65 $field.ErrorRadius $radius.field.HasError
            inline constexpr int ErrorRadius = 4;
            //! ComboBox.qss:50, SpinBox.qss:66 $field.ErrorLine $line.field.HasError.color
            inline constexpr QColor ErrorLine = Accent::Error;
            //! LineEdit.qss:33, ComboBox.qss:55, SpinBox.qss:72 $field.DisabledFill $fill.field.disabled
            inline constexpr QColor DisabledFill = Foundations::Fill::Disabled;
            //! LineEdit.qss:34, ComboBox.qss:56, SpinBox.qss:73 $field.DisabledText $text.field.disabled.Alt1
            inline constexpr QColor DisabledText = Foundations::Text::Disabled;
        } // namespace Field

        // ------------------------------------------------------------------------------
        // The secondary tab: TabWidget.qss's `AzQtComponents--TabBar.Secondary`, which TabElement draws and Script Canvas's
        // graph tabs restate
        // ------------------------------------------------------------------------------

        namespace Tab
        {
            inline constexpr QColor SecondaryText(0x95, 0x95, 0x95);  // TabWidget.qss:142 $tab.SecondaryText $text.tab.idle
            //! TabWidget.qss:147 $tab.SecondaryHoverFill $fill.tab.hover.Alt1
            inline constexpr QColor SecondaryHoverFill(0xFF, 0xFF, 0xFF, 0x26);
            //! Fragment of Low: the underline of an idle tab
            inline constexpr QColor SecondaryLine(0x33, 0x33, 0x33);  // TabWidget.qss:140 $tab.SecondaryLine $line.tab.idle.color.Low
        } // namespace Tab

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
