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

namespace O3DE::ProjectManager
{
    //! The paint values that the legacy GUI's code and the elements of the flattened GUI (FlatElements.cpp) both read, so that each one
    //! has a single source: the flattened system owns it. A named namespace, not an anonymous one: a unity build joins the files of a
    //! target into one translation unit, where anonymous namespaces merge.
    namespace FlatValues
    {
        // ------------------------------------------------------------------------------
        // The Gem Catalog: the header of the gem list, the tags of its filter and the cart
        // ------------------------------------------------------------------------------

        // The fill of the sheet the legacy GUI sets on the header of the gem list; it has no selector and so reaches every widget in the
        // header (GemListHeaderWidget.cpp:31 `background-color: #333333;`, GemListHeaderWidget::GemListHeaderWidget)
        // $fill.surface.idle.Low
        const QColor GemListHeaderFill = AzQtComponents::Foundations::Low;

        // The fill of the sheet the legacy GUI sets on a tag of the gem filter (GemFilterTagWidget.cpp:29 `background-color: #555555;`).
        // FilterTagWidget::FilterTagWidget $fill.surface.idle.Highest
        const QColor FilterTagFill = AzQtComponents::Foundations::Highest;

        // The fill of the sheet the legacy GUI sets on the list of the 'Manual setup is required' dialog; it has no selector and so
        // reaches the list, its viewport and its scroll bars (GemRequirementListView.cpp:19 `background-color: #444444;`).
        // GemRequirementListView::GemRequirementListView $fill.view.idle.Mid
        const QColor GemRequirementListFill = AzQtComponents::Foundations::Mid;

        // The cart panel's fill (ProjectManager.qss:811), and the triangle under the cart button that joins the button to the open
        // panel (GemCatalogHeaderWidget.cpp:628 `QBrush(QColor("#555555"))`). $fill.surface.idle.Highest
        const QColor CartFill = AzQtComponents::Foundations::Highest;

        // ------------------------------------------------------------------------------
        // The link label
        // ------------------------------------------------------------------------------

        // The colour of the sheets the legacy GUI sets on a link label, idle and hovered (LinkWidget.cpp:68 and :95 `color: #94D2FF;`).
        // LinkLabel::enterEvent, LinkLabel::SetDefaultStyle $text.surface.idle.Alt7
        const QColor LinkLabelColor(0x94, 0xD2, 0xFF);
    } // namespace FlatValues
} // namespace O3DE::ProjectManager
