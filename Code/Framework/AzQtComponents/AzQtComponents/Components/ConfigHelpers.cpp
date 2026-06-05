/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include <AzQtComponents/Components/ConfigHelpers.h>
#include <AzQtComponents/Components/StyleManagerInterface.h>

#include <AzCore/Interface/Interface.h>

#include <QPoint>
#include <QPixmap>
#include <QCursor>

namespace AzQtComponents
{
    namespace ConfigHelpers
    {
        /* Template specialization for QPixmap.
         *
         * Entry in *Config.ini should look like this:
         * [key]
         * Path=path/to/pixmap
         */
        template <>
        void read(QSettings& settings, const QString& key, QPixmap& pixmap)
        {
            QString path;
            ConfigHelpers::read<QString>(settings, key, path);

            const QPixmap testPixmap(path);
            if (!testPixmap.isNull())
            {
                pixmap = testPixmap;
            }
        }

        /* Template specialization for QCursor.
         *
         * Entry in *Config.ini should look like this:
         * [key]
         * Path=path/to/cursor/pixmap
         */
        template <>
        void read(QSettings& settings, const QString& key, QCursor& cursor)
        {
            QPixmap cursorPixmap;
            ConfigHelpers::read<QPixmap>(settings, key, cursorPixmap);

            if (!cursorPixmap.isNull())
            {
                cursor = QCursor(cursorPixmap);
            }
        }

        int themeInt(const char* token, int fallback)
        {
            auto* sm = AZ::Interface<StyleManagerInterface>::Get();
            return (sm && sm->IsStylePropertyDefined(token)) ? sm->GetStylePropertyAsInteger(token) : fallback;
        }

        qreal themeReal(const char* token, qreal fallback)
        {
            auto* sm = AZ::Interface<StyleManagerInterface>::Get();
            return (sm && sm->IsStylePropertyDefined(token)) ? static_cast<qreal>(sm->GetStylePropertyAsInteger(token)) : fallback;
        }

        QColor themeColor(const char* token, const QColor& fallback)
        {
            if (auto* sm = AZ::Interface<StyleManagerInterface>::Get(); sm && sm->IsStylePropertyDefined(token))
            {
                const QColor c = sm->GetStylePropertyAsColor(token);
                if (c.isValid())
                {
                    return c;
                }
            }
            return fallback;
        }
    } // namespace ConfigHelpers
} // namespace AzQtComponents
