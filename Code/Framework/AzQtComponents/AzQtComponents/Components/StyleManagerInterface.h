/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#pragma once

#include <AzQtComponents/AzQtComponentsAPI.h>

#include <AzCore/RTTI/RTTI.h>

#include <QColor>
#include <QString>

namespace AzQtComponents
{
    //! Interface for querying the active editor theme's style properties.
    //!
    //! Implemented by StyleManager and registered through AZ::Interface so that the stylesheet
    //! preprocessor (for $Variable substitution in .qss) and any runtime painting code can
    //! resolve theme values from a single source.
    class AZ_QT_COMPONENTS_API StyleManagerInterface
    {
    public:
        AZ_RTTI(StyleManagerInterface, "{92DFE816-91B7-4B71-9300-C404E9831A46}");

        virtual ~StyleManagerInterface() = default;

        //! Returns true if a property with the given key exists in the active theme.
        virtual bool IsStylePropertyDefined(const char* propertyKey) const = 0;

        //! Returns the raw string value for the key (empty if undefined). Used for $Variable
        //! substitution in stylesheets; the value is passed through to Qt verbatim, so functional
        //! color notations such as rgba(...) are preserved.
        virtual QString GetStylePropertyAsString(const char* propertyKey) const = 0;

        //! Returns the value parsed as an integer (0 if undefined / not numeric).
        virtual int GetStylePropertyAsInteger(const char* propertyKey) const = 0;

        //! Returns the value parsed as a color (invalid QColor if undefined). Handles #hex,
        //! named colors, and rgb()/rgba() functional notation.
        virtual QColor GetStylePropertyAsColor(const char* propertyKey) const = 0;
    };
} // namespace AzQtComponents
