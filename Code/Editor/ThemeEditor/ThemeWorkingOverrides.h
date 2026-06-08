/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */
#pragma once

#include <AzQtComponents/Components/StyleManager.h>

#include <QHash>
#include <QSettings>
#include <QString>
#include <QStringList>

// ============================================================================
// Theme working overrides
// ----------------------------------------------------------------------------
// The "working overrides" are theme edits the user has Applied to the editor but
// not yet saved into a named theme. They persist in shared QSettings so they
// survive an editor restart, and external tools (the Class Creation Wizard) read
// them so those tools match the editor's current look. They are tagged with the
// base theme name they sit on, so selecting a different theme discards them.
//
// Lifecycle:
//   Apply to Editor                 -> Save  (current live token map + base name)
//   Editor startup (after setTheme)  -> ApplyIfMatching (only if base name matches)
//   Reload / theme switch / Save As  -> Clear
//
// Storage: QSettings(org "O3DE", app "O3DE Editor"), group "ThemeWorkingOverrides".
// Each token is one key; BaseThemeKey holds the base theme folder name. These four
// constants are the cross-process contract -- the Class Wizard reads the same store.
// ============================================================================

namespace ThemeWorkingOverrides
{
    inline constexpr const char* OrgName       = "O3DE";
    inline constexpr const char* AppName       = "O3DE Editor";
    inline constexpr const char* GroupName     = "ThemeWorkingOverrides";
    inline constexpr const char* BaseThemeKey  = "__baseTheme";
    inline constexpr const char* EditorThemeKey = "Settings/EditorTheme";   // the editor's selected theme

    //! Publish the selected theme name to the shared store immediately. The editor otherwise only
    //! flushes this on settings-save/shutdown, so without this a relaunched external tool (the Class
    //! Wizard) would not see a theme switch until the editor is restarted.
    inline void SaveSelectedTheme(const QString& themeName)
    {
        QSettings settings(QString::fromUtf8(OrgName), QString::fromUtf8(AppName));
        settings.setValue(QString::fromUtf8(EditorThemeKey), themeName);
    }

    //! Persist the current applied token values as the unnamed working overlay.
    inline void Save(const QHash<QString, QString>& tokens, const QString& baseTheme)
    {
        QSettings settings(QString::fromUtf8(OrgName), QString::fromUtf8(AppName));
        settings.beginGroup(QString::fromUtf8(GroupName));
        settings.remove(QString());   // drop any stale keys first
        settings.setValue(QString::fromUtf8(BaseThemeKey), baseTheme);
        for (auto it = tokens.constBegin(); it != tokens.constEnd(); ++it)
        {
            settings.setValue(it.key(), it.value());
        }
        settings.endGroup();
    }

    //! Remove the working overlay (Reload, theme switch, Save As).
    inline void Clear()
    {
        QSettings settings(QString::fromUtf8(OrgName), QString::fromUtf8(AppName));
        settings.beginGroup(QString::fromUtf8(GroupName));
        settings.remove(QString());
        settings.endGroup();
    }

    //! After the base theme is loaded at startup: if a working overlay exists for
    //! currentTheme, re-apply each override and refresh. No-op otherwise.
    inline void ApplyIfMatching(const QString& currentTheme)
    {
        QSettings settings(QString::fromUtf8(OrgName), QString::fromUtf8(AppName));
        settings.beginGroup(QString::fromUtf8(GroupName));

        const QString base = settings.value(QString::fromUtf8(BaseThemeKey)).toString();
        if (base.isEmpty() || base != currentTheme)
        {
            settings.endGroup();
            return;
        }

        const QString baseKey = QString::fromUtf8(BaseThemeKey);
        bool applied = false;
        for (const QString& key : settings.allKeys())
        {
            if (key == baseKey)
            {
                continue;
            }
            AzQtComponents::StyleManager::setThemeProperty(key, settings.value(key).toString());
            applied = true;
        }
        settings.endGroup();

        if (applied)
        {
            AzQtComponents::StyleManager::reapplyTheme();
        }
    }
}
