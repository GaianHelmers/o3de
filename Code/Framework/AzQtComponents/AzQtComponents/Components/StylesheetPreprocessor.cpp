/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include <AzQtComponents/Components/StylesheetPreprocessor.h>
#include <AzQtComponents/Components/StyleManagerInterface.h>

#include <AzCore/Debug/Trace.h>
#include <AzCore/Interface/Interface.h>

#include <QObject>

namespace AzQtComponents
{
    StylesheetPreprocessor::StylesheetPreprocessor(QObject* pParent)
        : QObject(pParent)
    {
    }

    StylesheetPreprocessor::~StylesheetPreprocessor()
    {
    }

    void StylesheetPreprocessor::Initialize()
    {
        m_styleManagerInterface = AZ::Interface<StyleManagerInterface>::Get();
        AZ_Assert(m_styleManagerInterface, "StylesheetPreprocessor: StyleManagerInterface was not registered before Initialize().");
    }

    void StylesheetPreprocessor::ClearColorCache()
    {
        m_cachedColors.clear();
    }

    QString StylesheetPreprocessor::ProcessStyleSheet(const QString& stylesheetData)
    {
        enum class ParseState
        {
            Normal, Variable, Done
        };

        ParseState state = ParseState::Normal;
        QString out;
        QString varName;

        auto appendVariableValue = [this, &out](const QString& name)
        {
            if (m_styleManagerInterface && !name.isEmpty())
            {
                out.append(m_styleManagerInterface->GetStylePropertyAsString(name.toUtf8().constData()));
            }
        };

        auto i = stylesheetData.cbegin();
        while (state != ParseState::Done && i != stylesheetData.end())
        {
            while (state == ParseState::Normal && i != stylesheetData.end())
            {
                char c = i->toLatin1();
                switch (c)
                {
                case '$':
                    i++;
                    state = ParseState::Variable;
                    break;
                default:
                    out.append(*i);
                    i++;
                }
            }

            while (state == ParseState::Variable && i != stylesheetData.end())
            {
                char c = i->toLatin1();

                // All characters valid in an identifier
                if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || (c == '_'))
                {
                    varName.append(*i);
                    i++;
                }
                else
                {
                    // We are finished reading the current variable name
                    appendVariableValue(varName);
                    varName.clear();
                    out.append(*i);
                    i++;
                    state = ParseState::Normal;
                    break;
                }
            }
        }

        // A variable token that runs to the very end of the input has no trailing delimiter,
        // so flush it here.
        appendVariableValue(varName);

        return out;
    }

    const QColor& StylesheetPreprocessor::GetColorByName(const QString& name)
    {
        if (m_cachedColors.contains(name))
        {
            return m_cachedColors[name];
        }

        QColor color;
        if (m_styleManagerInterface)
        {
            color = m_styleManagerInterface->GetStylePropertyAsColor(name.toUtf8().constData());
        }

        m_cachedColors[name] = color;
        return m_cachedColors[name];
    }
} // namespace AzQtComponents
