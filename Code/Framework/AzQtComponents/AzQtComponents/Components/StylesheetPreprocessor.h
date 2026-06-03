/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#pragma once

#include <AzQtComponents/AzQtComponentsAPI.h>

#include <QColor>
#include <QHash>
#include <QObject>

namespace AzQtComponents
{
    class StyleManagerInterface;

    //! Substitutes $Variable tokens in stylesheets with values pulled from the active theme.
    //!
    //! Theme values are sourced from the StyleManagerInterface (implemented by StyleManager),
    //! cached once during Initialize(). The $ prefix is used (not @) because the stylesheet
    //! cache's @import preprocessing consumes the @ character.
    class AZ_QT_COMPONENTS_API StylesheetPreprocessor
        : public QObject
    {
        Q_OBJECT

    public:
        explicit StylesheetPreprocessor(QObject* pParent);
        ~StylesheetPreprocessor();

        //! Caches the registered StyleManagerInterface. Call after StyleManager has registered it.
        void Initialize();

        //! Replaces every $Variable token in the input with the matching theme property value.
        QString ProcessStyleSheet(const QString& stylesheetData);

        //! Programmatic theme color access (cached). Resolves via StyleManagerInterface.
        const QColor& GetColorByName(const QString& name);

        //! Drops the color cache so subsequent lookups reflect a newly loaded theme.
        void ClearColorCache();

    private:
        StyleManagerInterface* m_styleManagerInterface = nullptr;

        AZ_PUSH_DISABLE_WARNING(4251, "-Wunknown-warning-option") // 4251: QHash<QString,QColor> needs dll-interface to be used by clients of this class
        QHash<QString, QColor> m_cachedColors;
        AZ_POP_DISABLE_WARNING
    };
} // namespace AzQtComponents
