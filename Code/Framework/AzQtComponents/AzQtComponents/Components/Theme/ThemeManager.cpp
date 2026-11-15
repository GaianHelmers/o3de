/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include <AzQtComponents/Components/Theme/ThemeManager.h>

#include <AzCore/Utils/Utils.h>
#include <AzQtComponents/Components/Style.h>
#include <AzQtComponents/Components/Theme/ThemeTokens.h>

#include <QApplication>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QTimer>

namespace AzQtComponents
{
    //! Private to this file: named, not anonymous, as a unity build merges the anonymous namespaces of its files
    namespace ThemeManagerInternal
    {
        // ------------------------------------------------------------------------------
        // Values
        // ------------------------------------------------------------------------------

        ThemeManager* g_instance = nullptr;

        constexpr const char* FileSuffix = ".theme.json";
        constexpr const char* ResourceFolder = ":/AzQtComponents/Themes";   //!< the engine's themes in resources.qrc
        constexpr const char* UserFolderName = "Themes";                    //!< below the O3DE manifest folder
        constexpr const char* ChoiceFile = "themes.ini";                    //!< in the user's folder: the theme chosen last
        constexpr const char* ChoiceKey = "ActiveTheme";
        constexpr int SaveDelayMs = 400;        //!< an edited theme is written this long after its last edit
        constexpr int BaseLimit = 8;            //!< no theme lies over more themes than this: a ring of bases ends here

        // The keys of a theme file
        constexpr const char* NameKey = "name";
        constexpr const char* DescriptionKey = "description";
        constexpr const char* AuthorKey = "author";
        constexpr const char* BaseKey = "base";
        constexpr const char* ColorsKey = "colors";
        constexpr const char* StructureKey = "structure";

        // ------------------------------------------------------------------------------
        // Theme files
        // ------------------------------------------------------------------------------

        //! A value of a theme file as the text the tokens read: a string as it is, a number as a whole number, four numbers as
        //! margins. Empty: the file's value is none of these.
        QString valueText(const QJsonValue& value)
        {
            if (value.isString())
            {
                return value.toString();
            }
            if (value.isDouble())
            {
                return QString::number(qRound(value.toDouble()));
            }
            if (value.isArray() && value.toArray().size() == 4)
            {
                QStringList sides;
                for (const QJsonValue& side : value.toArray())
                {
                    sides.append(QString::number(qRound(side.toDouble())));
                }
                return sides.join(QLatin1Char(' '));
            }
            return QString();
        }

        //! True if the value belongs in a file's `colors`: its token is a colour, or (no such token here) it reads like one
        bool isColorValue(const QByteArray& key, const QString& text)
        {
            const ThemeTokens::Info info = ThemeTokens::info(key);
            if (!info.m_key.isEmpty())
            {
                return info.m_kind == ThemeKind::Color;
            }
            return text.startsWith(QLatin1Char('#'));
        }

        //! The name as a file name: letters, digits, spaces and - _ ( ) stay
        QString fileNameFor(const QString& name)
        {
            QString file;
            for (const QChar c : name)
            {
                const bool kept = c.isLetterOrNumber() || c == QLatin1Char(' ') || c == QLatin1Char('-') || c == QLatin1Char('_')
                    || c == QLatin1Char('(') || c == QLatin1Char(')');
                file.append(kept ? c : QLatin1Char('_'));
            }
            return file + QLatin1String(FileSuffix);
        }

        void setError(QString* error, const QString& text)
        {
            if (error)
            {
                *error = text;
            }
        }
    } // namespace ThemeManagerInternal

    using namespace ThemeManagerInternal;

    bool ThemeFile::read(const QString& path, ThemeFile& theme, QString* error)
    {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
        {
            setError(error, QObject::tr("The file cannot be opened: %1").arg(path));
            return false;
        }
        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
        if (!document.isObject())
        {
            setError(error, QObject::tr("The file is not a theme (%1): %2").arg(parseError.errorString(), path));
            return false;
        }
        const QJsonObject root = document.object();
        const QString name = root.value(QLatin1String(NameKey)).toString().trimmed();
        if (name.isEmpty())
        {
            setError(error, QObject::tr("The theme has no name: %1").arg(path));
            return false;
        }

        theme = ThemeFile();
        theme.m_name = name;
        theme.m_description = root.value(QLatin1String(DescriptionKey)).toString();
        theme.m_author = root.value(QLatin1String(AuthorKey)).toString();
        theme.m_base = root.value(QLatin1String(BaseKey)).toString().trimmed();
        // O3DE Original is below every theme: naming it as the base says the same as naming none
        if (theme.m_base == QLatin1String(ThemeManager::OriginalName))
        {
            theme.m_base.clear();
        }
        theme.m_path = path;
        for (const char* part : { ColorsKey, StructureKey })
        {
            const QJsonObject values = root.value(QLatin1String(part)).toObject();
            for (auto value = values.constBegin(); value != values.constEnd(); ++value)
            {
                const QString text = valueText(value.value());
                if (!text.isEmpty())
                {
                    theme.m_values.insert(value.key().toUtf8(), text);
                }
            }
        }
        return true;
    }

    bool ThemeFile::write(const QString& path, QString* error) const
    {
        QJsonObject colors;
        QJsonObject structure;
        for (auto value = m_values.constBegin(); value != m_values.constEnd(); ++value)
        {
            const QString key = QString::fromUtf8(value.key());
            if (isColorValue(value.key(), value.value()))
            {
                colors.insert(key, value.value());
                continue;
            }
            // A length is written as a number, anything else as its text
            bool isNumber = false;
            const int number = value.value().toInt(&isNumber);
            structure.insert(key, isNumber ? QJsonValue(number) : QJsonValue(value.value()));
        }

        QJsonObject root;
        root.insert(QLatin1String(NameKey), m_name);
        root.insert(QLatin1String(DescriptionKey), m_description);
        root.insert(QLatin1String(AuthorKey), m_author);
        root.insert(QLatin1String(BaseKey), m_base.isEmpty() ? QString::fromLatin1(ThemeManager::OriginalName) : m_base);
        root.insert(QLatin1String(ColorsKey), colors);
        root.insert(QLatin1String(StructureKey), structure);

        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        {
            setError(error, QObject::tr("The file cannot be written: %1").arg(path));
            return false;
        }
        file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
        return true;
    }

    // ----------------------------------------------------------------------------------
    // Life
    // ----------------------------------------------------------------------------------

    ThemeManager* ThemeManager::instance()
    {
        return g_instance;
    }

    ThemeManager::ThemeManager(const QString& builtInFolder, const QString& userFolder, QObject* parent)
        : QObject(parent)
        , m_builtInFolder(builtInFolder)
        , m_userFolder(userFolder)
        , m_active(QLatin1String(OriginalName))
        , m_saveTimer(new QTimer(this))
    {
        if (m_userFolder.isEmpty())
        {
            m_userFolder = qEnvironmentVariable(UserFolderVariable).trimmed();
        }
        if (m_userFolder.isEmpty())
        {
            const AZ::IO::FixedMaxPathString manifestFolder = AZ::Utils::GetO3deManifestDirectory();
            m_userFolder = QDir(QString::fromUtf8(manifestFolder.c_str())).filePath(QLatin1String(UserFolderName));
        }
        m_saveTimer->setSingleShot(true);
        m_saveTimer->setInterval(SaveDelayMs);
        connect(m_saveTimer, &QTimer::timeout, this, &ThemeManager::saveNow);

        g_instance = this;
        readThemes();

        // The theme of this run alone: a capture or a benchmark names it, and nothing is saved
        const QString named = qEnvironmentVariable(ThemeVariable).trimmed();
        if (!named.isEmpty())
        {
            m_choiceIsForThisRunOnly = true;
            const int delayMs = qEnvironmentVariableIntValue(ThemeDelayVariable);
            if (!contains(named))
            {
                qWarning() << "ThemeManager:" << ThemeVariable << "names no theme:" << named;
            }
            else if (delayMs > 0)
            {
                // A test of the change itself: the application starts as O3DE Original and changes once its windows exist
                QTimer::singleShot(delayMs, this, [this, named]
                {
                    setActiveTheme(named);
                });
            }
            else
            {
                m_active = named;
                apply();
            }
        }
    }

    ThemeManager::~ThemeManager()
    {
        saveNow();
        if (g_instance == this)
        {
            g_instance = nullptr;
        }
    }

    void ThemeManager::restoreSavedTheme()
    {
        ThemeManager* manager = g_instance;
        if (!manager || manager->m_choiceIsForThisRunOnly)
        {
            return;
        }
        const QSettings choice(QDir(manager->m_userFolder).filePath(QLatin1String(ChoiceFile)), QSettings::IniFormat);
        const QString saved = choice.value(QLatin1String(ChoiceKey)).toString();
        if (!saved.isEmpty() && saved != manager->m_active && manager->contains(saved))
        {
            manager->m_active = saved;
            manager->apply();
            emit manager->themesChanged();
        }
    }

    // ----------------------------------------------------------------------------------
    // The themes
    // ----------------------------------------------------------------------------------

    void ThemeManager::readThemes()
    {
        m_themes.clear();

        // The engine's: the folder of a source build, else the ones the library carries
        const QStringList filter{ QLatin1Char('*') + QLatin1String(FileSuffix) };
        QDir builtIn(m_builtInFolder);
        if (m_builtInFolder.isEmpty() || builtIn.entryList(filter, QDir::Files).isEmpty())
        {
            builtIn = QDir(QLatin1String(ResourceFolder));
        }
        for (int isUser = 0; isUser < 2; ++isUser)
        {
            const QDir folder = isUser ? QDir(m_userFolder) : builtIn;
            const QStringList files = folder.entryList(filter, QDir::Files, QDir::Name);
            for (const QString& file : files)
            {
                ThemeFile theme;
                QString error;
                if (!ThemeFile::read(folder.filePath(file), theme, &error))
                {
                    qWarning() << "ThemeManager:" << error;
                    continue;
                }
                if (contains(theme.m_name))
                {
                    qWarning() << "ThemeManager: a second theme of the name" << theme.m_name << "is left out:" << theme.m_path;
                    continue;
                }
                theme.m_builtIn = !isUser;
                m_themes.append(theme);
            }
        }
    }

    void ThemeManager::reload()
    {
        saveNow();
        readThemes();
        if (!contains(m_active))
        {
            m_active = QLatin1String(OriginalName);
        }
        apply();
        emit themesChanged();
    }

    QStringList ThemeManager::themes() const
    {
        QStringList names{ QLatin1String(OriginalName) };
        for (const ThemeFile& theme : m_themes)
        {
            names.append(theme.m_name);
        }
        return names;
    }

    bool ThemeManager::contains(const QString& name) const
    {
        return name == QLatin1String(OriginalName) || find(name);
    }

    bool ThemeManager::isBuiltIn(const QString& name) const
    {
        const ThemeFile* theme = find(name);
        return name == QLatin1String(OriginalName) || (theme && theme->m_builtIn);
    }

    ThemeFile ThemeManager::theme(const QString& name) const
    {
        if (const ThemeFile* found = find(name))
        {
            return *found;
        }
        ThemeFile theme;
        if (name == QLatin1String(OriginalName))
        {
            theme.m_name = name;
            theme.m_description = tr("The original look of the O3DE Editor and its tools: the values in the code.");
            theme.m_builtIn = true;
        }
        return theme;
    }

    QString ThemeManager::activeTheme() const
    {
        return m_active;
    }

    QString ThemeManager::userFolder() const
    {
        return m_userFolder;
    }

    ThemeManager::ApplyTiming ThemeManager::lastApplyTiming() const
    {
        return m_lastApply;
    }

    ThemeFile* ThemeManager::find(const QString& name)
    {
        for (ThemeFile& theme : m_themes)
        {
            if (theme.m_name == name)
            {
                return &theme;
            }
        }
        return nullptr;
    }

    const ThemeFile* ThemeManager::find(const QString& name) const
    {
        return const_cast<ThemeManager*>(this)->find(name);
    }

    QString ThemeManager::freeName(const QString& wanted) const
    {
        QString name = wanted;
        for (int number = 2; contains(name); ++number)
        {
            name = QStringLiteral("%1 %2").arg(wanted).arg(number);
        }
        return name;
    }

    QString ThemeManager::pathFor(const QString& name) const
    {
        return QDir(m_userFolder).filePath(fileNameFor(name));
    }

    // ----------------------------------------------------------------------------------
    // Choosing and applying
    // ----------------------------------------------------------------------------------

    QHash<QByteArray, QString> ThemeManager::valuesOf(const QString& name) const
    {
        // The chain from the theme down to the one that lies on O3DE Original, then its values from the bottom up
        QVector<const ThemeFile*> chain;
        const ThemeFile* at = find(name);
        while (at && chain.size() < BaseLimit && !chain.contains(at))
        {
            chain.append(at);
            at = find(at->m_base);
        }
        QHash<QByteArray, QString> values;
        for (auto theme = chain.crbegin(); theme != chain.crend(); ++theme)
        {
            for (auto value = (*theme)->m_values.constBegin(); value != (*theme)->m_values.constEnd(); ++value)
            {
                values.insert(value.key(), value.value());
            }
        }
        return values;
    }

    QVector<QByteArray> ThemeManager::apply()
    {
        m_previewing = false;
        QElapsedTimer timer;
        timer.start();
        const QVector<QByteArray> refused = ThemeTokens::setTheme(valuesOf(m_active));
        const qint64 tokensUs = timer.nsecsElapsed() / 1000;
        for (const QByteArray& key : refused)
        {
            qWarning() << "ThemeManager: the theme" << m_active << "gives" << key << "a value its token does not read";
        }
        rebuildAndPolish(tokensUs);
        return refused;
    }

    void ThemeManager::rebuildAndPolish(qint64 tokensUs)
    {
        QElapsedTimer timer;
        timer.start();
        ThemeTokens::rebuild();
        m_lastApply.m_tokensUs = tokensUs;
        m_lastApply.m_rebuildUs = timer.nsecsElapsed() / 1000;

        // No Style yet while StyleManager initializes: the application is polished with the theme's values from the start
        timer.restart();
        if (Style* style = qobject_cast<Style*>(QApplication::style()))
        {
            style->applyTheme();
        }
        m_lastApply.m_polishUs = timer.nsecsElapsed() / 1000;
        m_lastApply.m_widgets = static_cast<int>(QApplication::allWidgets().size());
        emit themeApplied();
    }

    bool ThemeManager::setActiveTheme(const QString& name)
    {
        if (!contains(name))
        {
            return false;
        }
        saveNow();
        m_active = name;
        saveChoice();
        apply();
        emit themesChanged();
        return true;
    }

    void ThemeManager::saveChoice() const
    {
        if (m_choiceIsForThisRunOnly)
        {
            return;
        }
        QDir().mkpath(m_userFolder);
        QSettings choice(QDir(m_userFolder).filePath(QLatin1String(ChoiceFile)), QSettings::IniFormat);
        choice.setValue(QLatin1String(ChoiceKey), m_active);
    }

    // ----------------------------------------------------------------------------------
    // Editing the theme in effect
    // ----------------------------------------------------------------------------------

    ThemeFile* ThemeManager::editable()
    {
        ThemeFile* active = find(m_active);
        if (active && !active->m_builtIn)
        {
            return active;
        }
        // One of the engine's themes: the edits go to a theme of the user's that lies over it
        ThemeFile theme;
        const bool isOriginal = m_active == QLatin1String(OriginalName);
        theme.m_name = freeName(isOriginal ? tr("My Theme") : tr("%1 (edited)").arg(m_active));
        theme.m_description = isOriginal ? tr("Based on %1.").arg(QLatin1String(OriginalName)) : tr("Based on %1.").arg(m_active);
        theme.m_base = isOriginal ? QString() : m_active;
        theme.m_path = pathFor(theme.m_name);
        m_themes.append(theme);
        m_active = theme.m_name;
        m_unsaved = true;
        saveNow();
        saveChoice();
        emit themesChanged();
        return find(m_active);
    }

    bool ThemeManager::setValue(const QByteArray& key, const QString& text)
    {
        endPreview();
        QElapsedTimer timer;
        timer.start();
        if (!ThemeTokens::setValue(key, text))
        {
            return false;
        }
        const qint64 tokensUs = timer.nsecsElapsed() / 1000;
        editable()->m_values.insert(key, text);
        saveSoon();
        rebuildAndPolish(tokensUs);
        return true;
    }

    QVector<QByteArray> ThemeManager::setValues(const QHash<QByteArray, QString>& values)
    {
        if (values.isEmpty())
        {
            return {};
        }
        // The theme takes them all, the tokens are written once, and what no token reads goes out of the theme again
        const QString name = editable()->m_name;
        for (auto value = values.constBegin(); value != values.constEnd(); ++value)
        {
            find(name)->m_values.insert(value.key(), value.value());
        }
        const QVector<QByteArray> refused = apply();
        for (const QByteArray& key : refused)
        {
            if (values.contains(key))
            {
                find(name)->m_values.remove(key);
            }
        }
        saveSoon();
        return refused;
    }

    void ThemeManager::preview(const QHash<QByteArray, QString>& values)
    {
        QHash<QByteArray, QString> shown = valuesOf(m_active);
        for (auto value = values.constBegin(); value != values.constEnd(); ++value)
        {
            shown.insert(value.key(), value.value());
        }
        m_previewing = true;
        QElapsedTimer timer;
        timer.start();
        ThemeTokens::setTheme(shown);
        rebuildAndPolish(timer.nsecsElapsed() / 1000);
    }

    void ThemeManager::endPreview()
    {
        if (m_previewing)
        {
            apply();
        }
    }

    void ThemeManager::resetValue(const QByteArray& key)
    {
        ThemeFile* active = find(m_active);
        if (!active || active->m_builtIn || !active->m_values.remove(key))
        {
            return;
        }
        saveSoon();
        apply();
    }

    void ThemeManager::resetAllValues()
    {
        ThemeFile* active = find(m_active);
        if (!active || active->m_builtIn || active->m_values.isEmpty())
        {
            return;
        }
        active->m_values.clear();
        saveSoon();
        apply();
    }

    bool ThemeManager::hasOwnValue(const QByteArray& key) const
    {
        const ThemeFile* active = find(m_active);
        return active && !active->m_builtIn && active->m_values.contains(key);
    }

    void ThemeManager::saveSoon()
    {
        m_unsaved = true;
        m_saveTimer->start();
    }

    void ThemeManager::saveNow()
    {
        m_saveTimer->stop();
        if (!m_unsaved)
        {
            return;
        }
        m_unsaved = false;
        const ThemeFile* active = find(m_active);
        if (!active || active->m_builtIn)
        {
            return;
        }
        QString error;
        if (!active->write(active->m_path, &error))
        {
            qWarning() << "ThemeManager:" << error;
        }
    }

    // ----------------------------------------------------------------------------------
    // The user's themes
    // ----------------------------------------------------------------------------------

    QString ThemeManager::duplicate(const QString& name, const QString& newName)
    {
        if (!contains(name) || newName.trimmed().isEmpty())
        {
            return QString();
        }
        saveNow();
        // One of the engine's themes is copied as a theme that lies over it and so follows it; one of the user's as it is
        ThemeFile copy;
        const ThemeFile* source = find(name);
        if (source && !source->m_builtIn)
        {
            copy = *source;
        }
        else
        {
            copy.m_base = name == QLatin1String(OriginalName) ? QString() : name;
            copy.m_description = tr("Based on %1.").arg(name);
        }
        copy.m_name = freeName(newName.trimmed());
        copy.m_builtIn = false;
        copy.m_path = pathFor(copy.m_name);
        QString error;
        if (!copy.write(copy.m_path, &error))
        {
            qWarning() << "ThemeManager:" << error;
            return QString();
        }
        m_themes.append(copy);
        emit themesChanged();
        return copy.m_name;
    }

    bool ThemeManager::rename(const QString& name, const QString& newName)
    {
        const QString wanted = newName.trimmed();
        ThemeFile* theme = find(name);
        if (!theme || theme->m_builtIn || wanted.isEmpty() || contains(wanted))
        {
            return false;
        }
        saveNow();
        const QString oldPath = theme->m_path;
        theme->m_name = wanted;
        theme->m_path = pathFor(wanted);
        QString error;
        if (!theme->write(theme->m_path, &error))
        {
            qWarning() << "ThemeManager:" << error;
            theme->m_name = name;
            theme->m_path = oldPath;
            return false;
        }
        if (oldPath != theme->m_path)
        {
            QFile::remove(oldPath);
        }
        // The themes that lie over it follow the new name
        for (ThemeFile& other : m_themes)
        {
            if (!other.m_builtIn && other.m_base == name)
            {
                other.m_base = wanted;
                other.write(other.m_path);
            }
        }
        if (m_active == name)
        {
            m_active = wanted;
            saveChoice();
        }
        emit themesChanged();
        return true;
    }

    bool ThemeManager::setDetails(const QString& description, const QString& author)
    {
        ThemeFile* active = find(m_active);
        if (!active || active->m_builtIn)
        {
            return false;
        }
        active->m_description = description.trimmed();
        active->m_author = author.trimmed();
        saveSoon();
        emit themesChanged();
        return true;
    }

    bool ThemeManager::remove(const QString& name)
    {
        const ThemeFile* theme = find(name);
        if (!theme || theme->m_builtIn)
        {
            return false;
        }
        m_saveTimer->stop();
        m_unsaved = m_unsaved && m_active != name;
        const QString base = theme->m_base;
        QFile::remove(theme->m_path);
        // The themes that lay over it lie over its base now
        for (ThemeFile& other : m_themes)
        {
            if (!other.m_builtIn && other.m_base == name)
            {
                other.m_base = base;
                other.write(other.m_path);
            }
        }
        for (int at = 0; at < m_themes.size(); ++at)
        {
            if (m_themes[at].m_name == name)
            {
                m_themes.removeAt(at);
                break;
            }
        }
        if (m_active == name)
        {
            m_active = contains(base) && !base.isEmpty() ? base : QString::fromLatin1(OriginalName);
            saveChoice();
            apply();
        }
        emit themesChanged();
        return true;
    }

    QString ThemeManager::importTheme(const QString& path, QString* error)
    {
        ThemeFile theme;
        if (!ThemeFile::read(path, theme, error))
        {
            return QString();
        }
        theme.m_name = freeName(theme.m_name);
        theme.m_builtIn = false;
        theme.m_path = pathFor(theme.m_name);
        if (!theme.write(theme.m_path, error))
        {
            return QString();
        }
        m_themes.append(theme);
        emit themesChanged();
        return theme.m_name;
    }

    bool ThemeManager::exportTheme(const QString& name, const QString& path, QString* error) const
    {
        if (!contains(name))
        {
            setError(error, tr("There is no theme of the name %1.").arg(name));
            return false;
        }
        // Every value the theme has through the themes below it, and its base by name: where the base exists the theme still follows
        // it for the tokens neither names
        ThemeFile out = theme(name);
        const QHash<QByteArray, QString> values = valuesOf(name);
        out.m_values.clear();
        for (auto value = values.constBegin(); value != values.constEnd(); ++value)
        {
            out.m_values.insert(value.key(), value.value());
        }
        return out.write(path, error);
    }
} // namespace AzQtComponents
