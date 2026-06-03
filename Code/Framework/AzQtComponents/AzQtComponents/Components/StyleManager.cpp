/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include <AzCore/Debug/Trace.h>
#include <AzCore/Interface/Interface.h>
#include <AzCore/IO/Path/Path.h>
#include <AzCore/Settings/SettingsRegistryMergeUtils.h>
#include <AzQtComponents/Components/StyleManager.h>
#include <QApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPalette>
#include <QStringList>
#include <QTextStream>
AZ_PUSH_DISABLE_WARNING(4251, "-Wunknown-warning-option") // 4251: 'QFileInfo::d_ptr': class 'QSharedDataPointer<QFileInfoPrivate>' needs to
                                                          // have dll-interface to be used by clients of class 'QFileInfo'
#include <QDir>
AZ_POP_DISABLE_WARNING
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QVector>
#include <QPointer>
#include <QString>
#include <QStyle>
#include <QStyleFactory>
#include <QWidget>

#include <AzQtComponents/Components/AutoCustomWindowDecorations.h>
#include <AzQtComponents/Components/Style.h>
#include <AzQtComponents/Components/StyleSheetCache.h>
#include <AzQtComponents/Components/StylesheetPreprocessor.h>
#include <AzQtComponents/Utilities/QtPluginPaths.h>

namespace AzQtComponents
{

    constexpr QStringView g_styleSheetRelativePath{ u"Code/Framework/AzQtComponents/AzQtComponents/Components/Widgets" };
    constexpr QStringView g_styleSheetResourcePath{ u":AzQtComponents/Widgets" };
    constexpr QStringView g_globalStyleSheetName{ u"BaseStyleSheet.qss" };
    constexpr QStringView g_searchPathPrefix{ u"AzQtComponentWidgets" };
    constexpr QStringView g_themePropertiesRelativePath{ u"Code/Framework/AzQtComponents/AzQtComponents/Themes" };
    constexpr QStringView g_themeSearchPathPrefix{ u"THEMES" };
    constexpr const char* g_themePropertiesKey = "theme_properties";

    StyleManager* StyleManager::s_instance = nullptr;

    static QStyle* createBaseStyle()
    {
        return QStyleFactory::create("Fusion");
    }

    // ============================================================================================
    // StyleManagerInterface - active theme property access
    // ============================================================================================

    bool StyleManager::IsStylePropertyDefined(const char* propertyKey) const
    {
        return m_themeProperties.contains(QString::fromUtf8(propertyKey));
    }

    QString StyleManager::GetStylePropertyAsString(const char* propertyKey) const
    {
        return m_themeProperties.value(QString::fromUtf8(propertyKey));
    }

    int StyleManager::GetStylePropertyAsInteger(const char* propertyKey) const
    {
        QString value = m_themeProperties.value(QString::fromUtf8(propertyKey)).trimmed();
        if (value.endsWith(QLatin1String("px")))
        {
            value.chop(2); // tolerate pixel-valued tokens such as "4px"
        }
        return value.toInt();
    }

    QColor StyleManager::GetStylePropertyAsColor(const char* propertyKey) const
    {
        const QString value = m_themeProperties.value(QString::fromUtf8(propertyKey));
        if (value.isEmpty())
        {
            return QColor();
        }

        // The QColor string constructor handles #hex and named colors, but not rgb()/rgba()
        // functional notation, so parse those components explicitly.
        if (value.startsWith(QStringLiteral("rgb")))
        {
            const int open = value.indexOf(QLatin1Char('('));
            const int close = value.indexOf(QLatin1Char(')'));
            if (open >= 0 && close > open)
            {
                const QStringList parts = value.mid(open + 1, close - open - 1).split(QLatin1Char(','), Qt::SkipEmptyParts);
                if (parts.size() == 3 || parts.size() == 4)
                {
                    const int r = parts[0].trimmed().toInt();
                    const int g = parts[1].trimmed().toInt();
                    const int b = parts[2].trimmed().toInt();
                    const int a = (parts.size() == 4) ? parts[3].trimmed().toInt() : 255;
                    return QColor(r, g, b, a);
                }
            }
            return QColor();
        }

        return QColor(value);
    }

    bool StyleManager::setTheme(const QString& themeName)
    {
        if (!s_instance)
        {
            AZ_Warning("StyleManager", false, "StyleManager::setTheme called before instance was created");
            return false;
        }

        if (themeName.isEmpty())
        {
            AZ_Warning("StyleManager", false, "StyleManager::setTheme called with an empty theme name");
            return false;
        }

        const QString themePropertiesPath = QStringLiteral("%1:%2/themeProperties.json")
            .arg(g_themeSearchPathPrefix.toString(), themeName);

        if (!s_instance->LoadThemePropertiesFromFile(themePropertiesPath))
        {
            return false;
        }

        s_instance->m_currentThemeName = themeName;
        s_instance->refresh();
        return true;
    }

    QString StyleManager::currentThemeName()
    {
        return s_instance ? s_instance->m_currentThemeName : QString();
    }

    void StyleManager::setThemeProperty(const QString& name, const QString& value)
    {
        if (!s_instance)
        {
            return;
        }
        s_instance->m_themeProperties[name] = value;
        if (s_instance->m_stylesheetPreprocessor)
        {
            s_instance->m_stylesheetPreprocessor->ClearColorCache();
        }
    }

    void StyleManager::reapplyTheme()
    {
        if (s_instance)
        {
            s_instance->refresh();
        }
    }

    QString StyleManager::themesRootPath()
    {
        return s_instance ? s_instance->m_themesRootPath : QString();
    }

    QVector<ThemeInfo> StyleManager::availableThemes()
    {
        QVector<ThemeInfo> themes;
        if (!s_instance || s_instance->m_themesRootPath.isEmpty())
        {
            return themes;
        }

        const QDir themesDir(s_instance->m_themesRootPath);
        const auto entries = themesDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for (const QFileInfo& entry : entries)
        {
            const QString themePropsPath = entry.absoluteFilePath() + QStringLiteral("/themeProperties.json");
            if (!QFile::exists(themePropsPath))
            {
                continue;
            }

            ThemeInfo info;
            info.folderName = entry.fileName();
            info.displayName = info.folderName;

            QFile file(themePropsPath);
            if (file.open(QFile::ReadOnly))
            {
                const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
                const QString name = root.value(QStringLiteral("theme_name")).toString();
                if (!name.isEmpty())
                {
                    info.displayName = name;
                }
            }
            themes.append(info);
        }
        return themes;
    }

    // ============================================================================================
    // Stylesheet management
    // ============================================================================================

    void StyleManager::addSearchPaths(
        const QString& searchPrefix, const QString& pathOnDisk, const QString& qrcPrefix, const AZ::IO::PathView& engineRootPath)
    {
        if (!s_instance)
        {
            qFatal("StyleManager::addSearchPaths called before instance was created");
            return;
        }

        s_instance->m_stylesheetCache->addSearchPaths(searchPrefix, pathOnDisk, qrcPrefix, engineRootPath);
    }

    bool StyleManager::setStyleSheet(QWidget* widget, QString styleFileName)
    {
        if (!s_instance)
        {
            qFatal("StyleManager::setStyleSheet called before instance was created");
            return false;
        }

        if (!widget)
        {
            qFatal("StyleManager::setStyleSheet called with null widget pointer");
            return false;
        }

        if (!styleFileName.endsWith(StyleSheetCache::styleSheetExtension()))
        {
            styleFileName.append(StyleSheetCache::styleSheetExtension());
        }

        const auto styleSheet = s_instance->m_stylesheetCache->loadStyleSheet(styleFileName);
        if (styleSheet.isEmpty())
        {
            return false;
        }

        s_instance->m_widgetToStyleSheetMap.insert(widget, styleFileName);

        connect(widget, &QObject::destroyed, s_instance, &StyleManager::stopTrackingWidget, Qt::UniqueConnection);

        widget->setStyleSheet(s_instance->m_stylesheetPreprocessor->ProcessStyleSheet(styleSheet));

        return true;
    }

    QStyle* StyleManager::styleSheetStyle(const QWidget* widget)
    {
        Q_UNUSED(widget);
        // widget is currently unused, but would be required if Qt::AA_ManualStyleSheetStyle was
        // not set.

        if (!s_instance)
        {
            AZ_Warning("StyleManager", false, "StyleManager::styleSheetStyle called before instance was created");
            return nullptr;
        }

        QObject* pParent = s_instance->m_style->parent();
        if (!pParent)
        {
            return s_instance->m_style;
        }

        QStyle* pStylesheetStyle = qobject_cast<QStyle*>(pParent);
        return pStylesheetStyle ? pStylesheetStyle : s_instance->m_style.get();
    }

    QStyle* StyleManager::baseStyle(const QWidget* widget)
    {
        const auto sss = styleSheetStyle(widget);
        return sss;
    }

    void StyleManager::repolishStyleSheet(QWidget* widget)
    {
        StyleManager::styleSheetStyle(widget)->polish(widget);
    }

    StyleManager::StyleManager(QObject* parent)
        : QObject(parent)
        , m_stylesheetPreprocessor(new StylesheetPreprocessor(this))
        , m_stylesheetCache(new StyleSheetCache(this))
    {
        if (s_instance)
        {
            qFatal("A StyleManager already exists");
        }
    }

    StyleManager::~StyleManager()
    {
        // Only unregister if this instance actually registered (initialize() may never have run).
        // Clear s_instance before deleting children so no child teardown observes a dying instance.
        if (s_instance == this)
        {
            AZ::Interface<StyleManagerInterface>::Unregister(this);
            s_instance = nullptr;
        }

        delete m_stylesheetPreprocessor;

        if (m_style)
        {
            delete m_style.data();
            m_style.clear();
        }
    }

    void StyleManager::initialize(QApplication* application, const AZ::IO::PathView& engineRootPath)
    {
        if (s_instance)
        {
            qFatal("StyleManager::Initialize called more than once");
            return;
        }
        s_instance = this;

        connect(application, &QCoreApplication::aboutToQuit, this, &StyleManager::cleanupStyles);

        initializeSearchPaths(application, engineRootPath);
        initializeFonts();

        QFont defaultFont("Open Sans");
        defaultFont.setPixelSize(12);
        QApplication::setFont(defaultFont);

        // Register the theme property interface and prime the preprocessor + default theme before
        // any stylesheet is applied, so $Variable substitution is active from the first apply.
        // Guard against double-registration in case initialize() is ever reached twice.
        if (AZ::Interface<StyleManagerInterface>::Get() == nullptr)
        {
            AZ::Interface<StyleManagerInterface>::Register(this);
        }
        m_stylesheetPreprocessor->Initialize();
        LoadThemePropertiesFromFile(QStringLiteral("%1:O3DE_Original/themeProperties.json").arg(g_themeSearchPathPrefix.toString()));
        m_currentThemeName = QStringLiteral("O3DE_Original");

        // The window decoration wrappers require the titlebar overdraw handler
        // so we can't initialize the custom window decoration monitor until the
        // titlebar overdraw handler has been initialized.
        m_autoCustomWindowDecorations = new AutoCustomWindowDecorations(this);
        m_autoCustomWindowDecorations->setMode(AutoCustomWindowDecorations::Mode_AnyWindow);

        // Order matters, need to setStylesheet() first, then when we call setStyle()
        // QT 6.8.3 implementation will create a (private) QStyleSheetStyle with our stylesheet, and use our custom QStyle class below.
        const auto globalStyleSheet = m_stylesheetCache->loadStyleSheet(g_globalStyleSheetName.toString());
        application->setStyleSheet(m_stylesheetPreprocessor->ProcessStyleSheet(globalStyleSheet));

        // Style is chained as: Style -> QStyleSheetStyle -> native, meaning any CSS limitation can be tackled in Style.cpp
        m_style = new Style(createBaseStyle());

        QApplication::setStyle(m_style);
        refresh();

        connect(
            m_stylesheetCache,
            &StyleSheetCache::styleSheetsChanged,
            this,
            [this]
            {
                refresh();
            });
    }

    void StyleManager::cleanupStyles()
    {
        QApplication::setStyle(createBaseStyle());
    }

    void StyleManager::stopTrackingWidget(QObject* object)
    {
        const auto widget = qobject_cast<QWidget* const>(object);
        if (!widget)
        {
            return;
        }

        m_widgetToStyleSheetMap.remove(widget);

        // Remove any old stylesheet
        widget->setStyleSheet(QString());
    }

    void StyleManager::initializeFonts()
    {
        // yes, the path specifier could've included OpenSans- and .ttf, but I
        // wanted anyone searching for OpenSans-Bold.ttf to find something so left it this way
        QString openSansPathSpecifier = QStringLiteral(":/AzQtFonts/Fonts/Open_Sans/%1");
        QFontDatabase::addApplicationFont(openSansPathSpecifier.arg("OpenSans-Bold.ttf"));
        QFontDatabase::addApplicationFont(openSansPathSpecifier.arg("OpenSans-BoldItalic.ttf"));
        QFontDatabase::addApplicationFont(openSansPathSpecifier.arg("OpenSans-ExtraBold.ttf"));
        QFontDatabase::addApplicationFont(openSansPathSpecifier.arg("OpenSans-ExtraBoldItalic.ttf"));
        QFontDatabase::addApplicationFont(openSansPathSpecifier.arg("OpenSans-Italic.ttf"));
        QFontDatabase::addApplicationFont(openSansPathSpecifier.arg("OpenSans-Light.ttf"));
        QFontDatabase::addApplicationFont(openSansPathSpecifier.arg("OpenSans-LightItalic.ttf"));
        QFontDatabase::addApplicationFont(openSansPathSpecifier.arg("OpenSans-Regular.ttf"));
        QFontDatabase::addApplicationFont(openSansPathSpecifier.arg("OpenSans-Semibold.ttf"));
        QFontDatabase::addApplicationFont(openSansPathSpecifier.arg("OpenSans-SemiboldItalic.ttf"));
    }

    void StyleManager::initializeSearchPaths([[maybe_unused]] QApplication* application, const AZ::IO::PathView& engineRootPath)
    {
        // now that QT is initialized, we can use its path manipulation functions to set the rest up:

        QString rootDir = QString::fromUtf8(engineRootPath.Native().data(), aznumeric_cast<int>(engineRootPath.Native().size()));

        if (!rootDir.isEmpty())
        {
            QDir appPath(rootDir);

            // Set the StyleSheetCache fallback prefix
            const auto pathOnDisk = appPath.absoluteFilePath(g_styleSheetRelativePath.toString());
            m_stylesheetCache->setFallbackSearchPaths(g_searchPathPrefix.toString(), pathOnDisk, g_styleSheetResourcePath.toString());

            // add the expected editor paths
            // this allows you to refer to your assets relative, like
            // STYLESHEETIMAGES:something.txt
            // UI:blah/blah.png
            // EDITOR:blah/something.txt
            QDir::addSearchPath("STYLESHEETIMAGES", appPath.filePath("Assets/Editor/Styles/StyleSheetImages"));
            QDir::addSearchPath("UI", appPath.filePath("Assets/Editor/UI"));
            QDir::addSearchPath("EDITOR", appPath.filePath("Assets/Editor"));

            // theme properties (THEMES:O3DE_Original/themeProperties.json, etc.)
            m_themesRootPath = appPath.absoluteFilePath(g_themePropertiesRelativePath.toString());
            QDir::addSearchPath(g_themeSearchPathPrefix.toString(), m_themesRootPath);
        }
    }

    // ============================================================================================
    // Theme property loading
    // ============================================================================================

    bool StyleManager::LoadThemeFileWithBase(const QString& filePath, int depth)
    {
        constexpr int maxThemeInheritanceDepth = 8;
        if (depth > maxThemeInheritanceDepth)
        {
            AZ_Warning("StyleManager", false, "StyleManager theme inheritance too deep (possible cycle) at: %s", filePath.toUtf8().constData());
            return false;
        }

        if (!QFile::exists(filePath))
        {
            AZ_Warning("StyleManager", false, "StyleManager could not load theme properties file: %s", filePath.toUtf8().constData());
            return false;
        }

        QFile themeFile(filePath);
        if (!themeFile.open(QFile::ReadOnly))
        {
            AZ_Warning("StyleManager", false, "StyleManager could not open theme properties file: %s", filePath.toUtf8().constData());
            return false;
        }

        const QJsonDocument doc = QJsonDocument::fromJson(themeFile.readAll());
        const QJsonObject rootObject = doc.object();

        // Load the base theme first (if specified) so this theme's properties override the base's.
        const QString themeBase = rootObject.value(QLatin1String("theme_base")).toString();
        if (!themeBase.isEmpty())
        {
            const QString basePath = QStringLiteral("%1:%2/themeProperties.json").arg(g_themeSearchPathPrefix.toString(), themeBase);
            LoadThemeFileWithBase(basePath, depth + 1);
        }

        // Overlay this theme's properties on top of the (already-loaded) base.
        if (rootObject.contains(QLatin1String(g_themePropertiesKey)))
        {
            LoadThemePropertiesRecursively(QString(), rootObject.value(QLatin1String(g_themePropertiesKey)).toObject());
        }
        return true;
    }

    void StyleManager::LoadThemePropertiesRecursively(const QString& prefix, const QJsonObject& jsonObject)
    {
        // Nested keys are flattened by concatenation (Text.Color -> "TextColor"); an empty "" key
        // contributes nothing to the name, so it denotes a group's default/base state.
        for (const QString& key : jsonObject.keys())
        {
            const QJsonValue value = jsonObject.value(key);
            if (value.isObject())
            {
                LoadThemePropertiesRecursively(prefix + key, value.toObject());
            }
            else
            {
                m_themeProperties[prefix + key] = value.toString();
            }
        }
    }

    bool StyleManager::LoadThemePropertiesFromFile(const QString& filePath)
    {
        // Start a fresh theme load: clear, then resolve the base->override inheritance chain.
        m_themeProperties.clear();
        if (m_stylesheetPreprocessor)
        {
            m_stylesheetPreprocessor->ClearColorCache();
        }

        // Default-fill from O3DE_Original first so no theme can ever render a blank token. Themes are
        // authored complete (self-contained), but any property a theme happens to omit -- e.g. a
        // community theme created before a token was added -- falls back to the canonical O3DE_Original
        // value instead of resolving to an empty string. This only covers gaps; a theme's own values
        // always win because they are loaded second and overwrite.
        const QString originalPath =
            QStringLiteral("%1:O3DE_Original/themeProperties.json").arg(g_themeSearchPathPrefix.toString());
        if (filePath != originalPath)
        {
            LoadThemeFileWithBase(originalPath, 0);
        }

        return LoadThemeFileWithBase(filePath, 0);
    }

    void StyleManager::refresh()
    {
        const auto globalStyleSheet = m_stylesheetCache->loadStyleSheet(g_globalStyleSheetName.toString());
        qApp->setStyleSheet(m_stylesheetPreprocessor->ProcessStyleSheet(globalStyleSheet));

        // Iterate widgets and update the stylesheet (the base style has already been set)
        auto i = m_widgetToStyleSheetMap.constBegin();
        while (i != m_widgetToStyleSheetMap.constEnd())
        {
            const auto styleSheet = m_stylesheetCache->loadStyleSheet(i.value());
            i.key()->setStyleSheet(m_stylesheetPreprocessor->ProcessStyleSheet(styleSheet));
            ++i;
        }

        // QMessageBox uses "QMdiSubWindowTitleBar" class to query the titlebar font
        // through QApplication::font() and (buggily) calculate required width of itself
        // to fit the title. It bypassess stylesheets. See QMessageBoxPrivate::updateSize().
        QFont titleBarFont("Open Sans");
        titleBarFont.setPixelSize(18);
        QApplication::setFont(titleBarFont, "QMdiSubWindowTitleBar");
    }

    const QColor& StyleManager::getColorByName(const QString& name)
    {
        return m_stylesheetPreprocessor->GetColorByName(name);
    }
} // namespace AzQtComponents

#if defined(AZ_QT_COMPONENTS_STATIC)
  // If we're statically compiling the lib, we need to include the compiled rcc resources
// somewhere to ensure that the linker doesn't optimize the symbols out (with Visual Studio at least)
// With dlls, there's no step to optimize out the symbols, so we don't need to do this.
#include <Components/rcc_resources.h>
#endif // #if defined(AZ_QT_COMPONENTS_STATIC)
