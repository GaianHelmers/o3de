/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#pragma once

#include <QColor>

namespace EMStudioFlatValues
{
    //! The paint values that the legacy GUI's inline sheets and the elements of the flattened GUI (EMStudioFlatSheet.cpp) both read,
    //! so that each one has a single source: the flattened system owns it, and the text of a sheet is written from it. A named
    //! namespace, not an anonymous one: a unity build joins the files of a target into one translation unit, where anonymous namespaces
    //! merge. The lines cited are those of the files before the flattening, when the sheets were the strings of their sites.

    // ------------------------------------------------------------------------------
    // The Animation Editor's dialogs
    // ------------------------------------------------------------------------------

    // The text of the labels of the keyboard shortcut dialog while the key is taken: the sheet `color: rgb(244,156,28)` of both labels
    // (KeyboardShortcutsWindow.cpp:417, :418 before the flattening) (ShortcutReceiverDialog::UpdateInterface) $text.surface.idle.Alt17
    inline constexpr QColor ConflictText(244, 156, 28);

    // The fill of the error report dialog, a sheet on the dialog (MainWindow.cpp:123 before the flattening).
    // ErrorWindow::ErrorWindow $fill.surface.error.Alt1
    inline constexpr QColor ErrorWindowBackground(30, 30, 30);

    // The fill of the strip behind the title of the reset workspace dialog (ResetSettingsDialog.cpp:56 before the flattening).
    // ResetSettingsDialog::ResetSettingsDialog $fill.surface.idle.top.Alt1
    inline constexpr QColor ResetSettingsTopLabelBackground(40, 40, 40);

    // The line of an input field while its text is not valid, `border: 1px solid red;` (EMStudioManager.cpp:262, and
    // LineEditValidatable.cpp:46) (EMStudioManager::SetWidgetAsInvalidInput) $line.surface.error.color.Alt1
    inline constexpr QColor InvalidInputLine(255, 0, 0);

    // The width of that line, the same sheets EMStudioManager::SetWidgetAsInvalidInput, LineEditValidatable::OnTextChanged
    // $line.surface.error.width.Alt1
    inline constexpr int InvalidInputLineWidth = 1;

    // The fill of the name field of a disabled time track, `background-color: rgb(70, 70, 70)` (TrackHeaderWidget.cpp:237 before the
    // flattening) HeaderTrackWidget::HeaderTrackWidget $text.header.disabled.Alt2
    inline constexpr QColor DisabledNameEdit(70, 70, 70);

    // ------------------------------------------------------------------------------
    // The anim graph editor and the parameter editor
    // ------------------------------------------------------------------------------

    // The line of the motion set combo box while the anim graph is used and no motion set is chosen, the CSS colour name orange
    // (AnimGraphEditor.cpp:229, :245 before the flattening) (AnimGraphEditor::UpdateMotionSetComboBox) $line.surface.warning.color.Alt1
    inline constexpr QColor WarningBorder(255, 165, 0);

    // The text of the trees of the hierarchy and navigate widgets (AnimGraphHierarchyWidget.cpp:67, NavigateWidget.cpp:71 before the
    // flattening) AnimGraphHierarchyWidget::AnimGraphHierarchyWidget $text.view.idle.Alt4
    inline constexpr QColor GraphTreeText(233, 233, 233);

    // The dashed line of the preview frame of the parameter editor (ParameterCreateEditWidget.cpp:72 before the flattening).
    // ParameterCreateEditWidget::ParameterCreateEditWidget $line.surface.idle.color.Alt12
    inline constexpr QColor PreviewBorder(151, 151, 151);

    // The fill of that frame. The style sheet text was #85858580, which Qt reads as #AARRGGBB: alpha 0x85 over rgb(133, 133, 128)
    // (ParameterCreateEditWidget.cpp:72 before the flattening) (ParameterCreateEditWidget::ParameterCreateEditWidget)
    // $fill.surface.idle.Alt22
    inline constexpr QColor PreviewBackground(133, 133, 128, 133);

    // ------------------------------------------------------------------------------
    // The command bar
    // ------------------------------------------------------------------------------

    // The progress text (CommandBarPlugin.cpp:93 before the flattening) (CommandBarPlugin::Init) $text.progress.idle.Alt2
    inline constexpr QColor ProgressText(140, 140, 140);

    // The result field's text after a command failed (CommandBarPlugin.cpp:166 before the flattening).
    // CommandBarPlugin::OnEnter $text.bar.error.Alt1
    inline constexpr QColor ResultErrorText(255, 0, 0);

    // The result field's text after a command succeeded (CommandBarPlugin.cpp:172 before the flattening).
    // CommandBarPlugin::OnEnter $text.bar.idle.Alt1
    inline constexpr QColor ResultSuccessText(0, 255, 0);

    // ------------------------------------------------------------------------------
    // The property widgets of the anim graph editor and the simulated objects
    // ------------------------------------------------------------------------------

    // The fill of the header of a notification (NotificationWidget.cpp:26 before the flattening).
    // NotificationWidget::NotificationWidget $fill.header.idle.Alt5
    inline constexpr QColor NotificationHeaderBackground(60, 60, 60);

    // The text of the X label of a blend space motion's position, the colour name red the site passes to the label's sheet
    // (BlendSpaceMotionContainerHandler.cpp:69) (BlendSpaceMotionWidget::BlendSpaceMotionWidget) $text.surface.idle.Alt10
    inline constexpr QColor AxisXText(255, 0, 0);

    // The text of the Y label, the colour name green, which is rgb(0, 128, 0) and not Qt::green (BlendSpaceMotionContainerHandler.cpp:73).
    // $text.surface.idle.Alt18
    inline constexpr QColor AxisYText(0, 128, 0);

    // The line of the label of a blend space motion that the motion set does not contain, `border: 1px solid red`
    // (BlendSpaceMotionContainerHandler.cpp:152) (BlendSpaceMotionWidget::UpdateInterface) $line.surface.error.color.Alt1
    inline constexpr QColor InvalidMotionLine(255, 0, 0);

    // Its width BlendSpaceMotionWidget::UpdateInterface $line.surface.error.width.Alt1
    inline constexpr int InvalidMotionLineWidth = 1;
} // namespace EMStudioFlatValues
