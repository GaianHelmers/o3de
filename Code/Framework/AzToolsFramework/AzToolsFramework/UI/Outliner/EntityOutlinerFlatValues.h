/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#pragma once

#include <AzQtComponents/Components/Elements/Foundations.h>

#include <QColor>

namespace AzToolsFramework
{
    // =======================================================================
    // Outliner Paint Colours
    // =======================================================================

    //! The colours the Entity Outliner paints with itself (its tree view and the delegate of its list model), named once, so the
    //! legacy and the flattened GUI read one value. The colours of the prefab rows are PrefabUiHandlerFlat's (PrefabUiHandler.h). A named
    //! namespace, not an anonymous one: a header is read by many files, and a unity build joins files into one translation unit, where
    //! anonymous namespaces merge.
    namespace EntityOutlinerFlat
    {
        //! The wash over a selected row: EntityOutlinerTreeView.hxx:99 (m_selectedColor), EntityOutlinerListModel.hxx:373
        //! (s_selectedColor) EntityOutlinerTreeView::m_selectedColor $fill.view.selected.Alt3
        inline const QColor& SelectedWash = AzQtComponents::Foundations::Outliner::SelectedWash;

        //! The wash over the row the pointer is on: EntityOutlinerTreeView.hxx:100 (m_hoverColor), EntityOutlinerListModel.hxx:374
        //! (s_hoverColor) EntityOutlinerTreeView::m_hoverColor $fill.view.hover.Alt2
        inline const QColor& HoverWash = AzQtComponents::Foundations::Outliner::HoverWash;

        //! The fill of the rectangle a drag selection draws: EntityOutlinerTreeView.hxx:101 (EntityOutlinerTreeView::m_dragSelectRectColor)
        //! $fill.view.selected.Alt2
        inline constexpr QColor DragSelectFill(255, 255, 255, 20);

        //! The outline of the rectangle a drag selection draws: EntityOutlinerTreeView.hxx:102
        //! (EntityOutlinerTreeView::m_dragSelectBorderColor) $line.view.selected.color.Alt1
        inline constexpr QColor DragSelectBorder(255, 255, 255);

        //! The disc behind the read-only badge of a row: EntityOutlinerListModel.hxx:376
        //! (EntityOutlinerItemDelegate::s_readOnlyBackgroundColor) $fill.view.idle.Mid
        inline const QColor& ReadOnlyBadgeBackground = AzQtComponents::Foundations::Mid;
    } // namespace EntityOutlinerFlat
} // namespace AzToolsFramework
