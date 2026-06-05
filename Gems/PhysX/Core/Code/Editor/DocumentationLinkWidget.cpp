/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include <Editor/DocumentationLinkWidget.h>

#include <AzQtComponents/Components/StyleManagerInterface.h>
#include <AzCore/Interface/Interface.h>

#include <QColor>

namespace PhysX
{
    namespace Editor
    {
        DocumentationLinkWidget::DocumentationLinkWidget(const QString& format, const QString& address)
            : QLabel()
        {
            setText(format.arg(address));
            setTextInteractionFlags(Qt::TextBrowserInteraction);
            setOpenExternalLinks(true);
            setAlignment(Qt::AlignCenter);
            setContentsMargins(60, 7, 60, 7);
            setWordWrap(true);
            // Banner background follows the active theme (value-preserving #333333 in O3DE_Original).
            QColor bannerColor(51, 51, 51);
            if (auto* styleManager = AZ::Interface<AzQtComponents::StyleManagerInterface>::Get();
                styleManager && styleManager->IsStylePropertyDefined("CardHeaderColor"))
            {
                const QColor themed = styleManager->GetStylePropertyAsColor("CardHeaderColor");
                if (themed.isValid())
                {
                    bannerColor = themed;
                }
            }
            setStyleSheet(QStringLiteral("background-color: %1;").arg(bannerColor.name()));
        }
    }
}
