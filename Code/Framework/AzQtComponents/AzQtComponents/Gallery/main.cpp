/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include <AzCore/PlatformIncl.h>
#include <AzCore/Asset/AssetManagerComponent.h>
#include <AzCore/Component/ComponentApplication.h>
#include <AzCore/Component/ComponentApplicationBus.h>
#include <AzCore/IO/Path/Path.h>
#include <AzCore/Settings/SettingsRegistryMergeUtils.h>
#include <AzCore/IO/Streamer/StreamerComponent.h>
#include <AzCore/Jobs/JobManagerComponent.h>
#include <AzCore/Memory/PoolAllocator.h>
#include <AzCore/Task/TaskGraphSystemComponent.h>
#include <AzCore/UserSettings/UserSettingsComponent.h>

#include <AzFramework/Asset/CustomAssetTypeComponent.h>

#include <AzToolsFramework/UI/PropertyEditor/PropertyManagerComponent.h>

#include <AzQtComponents/Components/StyledDockWidget.h>
#include <AzQtComponents/Components/O3DEStylesheet.h>
#include <AzQtComponents/Components/Theme/ThemeEditor.h>
#include <AzQtComponents/Components/Theme/ThemeManager.h>
#include <AzQtComponents/Utilities/HandleDpiAwareness.h>
#include <AzQtComponents/Components/WindowDecorationWrapper.h>
#include "ComponentDemoWidget.h"

#include <AzCore/Memory/SystemAllocator.h>

#include <QApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMainWindow>
#include <QSettings>
#include <QTimer>

#include <iostream>

const QString g_ui_1_0_SettingKey = QStringLiteral("useUI_1_0");

static void LogToDebug([[maybe_unused]] QtMsgType Type, [[maybe_unused]] const QMessageLogContext& Context, const QString& message)
{
    AZ::Debug::Platform::OutputToDebugger("Qt", message.toStdString().c_str());
    AZ::Debug::Platform::OutputToDebugger({}, "\n");
}

/*
 * Sets up and tears down everything we need for an AZ::ComponentApplication.
 * This is required for the ReflectedPropertyEditorPage.
 */
class ComponentApplicationWrapper
{
public:
    explicit ComponentApplicationWrapper()
    {
        AZ::ComponentApplication::Descriptor appDesc;
        m_systemEntity = m_componentApp.Create(appDesc);

        m_componentApp.RegisterComponentDescriptor(AZ::AssetManagerComponent::CreateDescriptor());
        m_componentApp.RegisterComponentDescriptor(AZ::JobManagerComponent::CreateDescriptor());
        m_componentApp.RegisterComponentDescriptor(AZ::TaskGraphSystemComponent::CreateDescriptor());
        m_componentApp.RegisterComponentDescriptor(AZ::StreamerComponent::CreateDescriptor());
        m_componentApp.RegisterComponentDescriptor(AZ::UserSettingsComponent::CreateDescriptor());
        m_componentApp.RegisterComponentDescriptor(AzFramework::CustomAssetTypeComponent::CreateDescriptor());
        m_componentApp.RegisterComponentDescriptor(AzToolsFramework::Components::PropertyManagerComponent::CreateDescriptor());

        m_systemEntity->CreateComponent<AZ::AssetManagerComponent>();
        m_systemEntity->CreateComponent<AZ::JobManagerComponent>();
        m_systemEntity->CreateComponent<AZ::TaskGraphSystemComponent>();
        m_systemEntity->CreateComponent<AZ::StreamerComponent>();
        m_systemEntity->CreateComponent<AZ::UserSettingsComponent>();
        m_systemEntity->CreateComponent<AzFramework::CustomAssetTypeComponent>();
        m_systemEntity->CreateComponent<AzToolsFramework::Components::PropertyManagerComponent>();

        m_systemEntity->Init();
        m_systemEntity->Activate();

        m_serializeContext = m_componentApp.GetSerializeContext();
        m_serializeContext->CreateEditContext();
    }

    ~ComponentApplicationWrapper()
    {
        m_serializeContext->DestroyEditContext();
        m_systemEntity->Deactivate();

        std::vector<AZ::Component*> components =
        {
            m_systemEntity->FindComponent<AZ::AssetManagerComponent>(),
            m_systemEntity->FindComponent<AZ::JobManagerComponent>(),
            m_systemEntity->FindComponent<AZ::TaskGraphSystemComponent>(),
            m_systemEntity->FindComponent<AZ::StreamerComponent>(),
            m_systemEntity->FindComponent<AZ::UserSettingsComponent>(),
            m_systemEntity->FindComponent<AzFramework::CustomAssetTypeComponent>(),
            m_systemEntity->FindComponent<AzToolsFramework::Components::PropertyManagerComponent>()
        };

        for (auto component : components)
        {
            m_systemEntity->RemoveComponent(component);
            delete component;
        }

        m_componentApp.UnregisterComponentDescriptor(AZ::AssetManagerComponent::CreateDescriptor());
        m_componentApp.UnregisterComponentDescriptor(AZ::JobManagerComponent::CreateDescriptor());
        m_componentApp.UnregisterComponentDescriptor(AZ::StreamerComponent::CreateDescriptor());
        m_componentApp.UnregisterComponentDescriptor(AZ::TaskGraphSystemComponent::CreateDescriptor());
        m_componentApp.UnregisterComponentDescriptor(AZ::UserSettingsComponent::CreateDescriptor());
        m_componentApp.UnregisterComponentDescriptor(AzFramework::CustomAssetTypeComponent::CreateDescriptor());
        m_componentApp.UnregisterComponentDescriptor(AzToolsFramework::Components::PropertyManagerComponent::CreateDescriptor());

        m_componentApp.Destroy();
    }

private:
    // ComponentApplication must not be a pointer - it cannot be dynamically allocated
    AZ::ComponentApplication m_componentApp;
    AZ::Entity* m_systemEntity = nullptr;
    AZ::SerializeContext* m_serializeContext = nullptr;
};

//! --theme-bench <file>: every theme in turn, then one value changed back and forth, each timed from the call until the event
//! loop is idle (the paints the change asked for), with the theme manager's own split of the cost. Written as JSON.
static void RunThemeBench(const QString& path)
{
    using AzQtComponents::ThemeManager;
    constexpr int Rounds = 10;
    constexpr int Edits = 40;

    ThemeManager* manager = ThemeManager::instance();
    QJsonArray runs;

    // The widgets alive, by class: a change of theme polishes every widget again, and must leave no widget behind
    const auto widgetsByClass = []
    {
        QHash<QString, int> counts;
        const QWidgetList all = QApplication::allWidgets();
        for (const QWidget* widget : all)
        {
            ++counts[QString::fromLatin1(widget->metaObject()->className())];
        }
        return counts;
    };
    QApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    const QHash<QString, int> widgetsBefore = widgetsByClass();
    const auto timed = [&runs, manager](const QString& what, const auto& change)
    {
        QElapsedTimer timer;
        timer.start();
        change();
        const qint64 appliedUs = timer.nsecsElapsed() / 1000;
        QApplication::processEvents();
        const qint64 paintedUs = timer.nsecsElapsed() / 1000;
        const ThemeManager::ApplyTiming timing = manager->lastApplyTiming();
        runs.append(QJsonObject{
            { "what", what },
            { "applied_us", appliedUs },
            { "painted_us", paintedUs },
            { "tokens_us", timing.m_tokensUs },
            { "rebuild_us", timing.m_rebuildUs },
            { "polish_us", timing.m_polishUs },
            { "widgets", timing.m_widgets },
        });
    };

    const QStringList themes = manager->themes();
    for (int round = 0; round < Rounds; ++round)
    {
        for (const QString& name : themes)
        {
            timed(QStringLiteral("theme: ") + name, [manager, name]
            {
                manager->setActiveTheme(name);
            });
        }
    }
    for (int edit = 0; edit < Edits; ++edit)
    {
        timed(QStringLiteral("value"), [manager, edit]
        {
            manager->setValue("accent.Primary", edit % 2 == 0 ? QStringLiteral("#3A7BD5") : QStringLiteral("#D57B3A"));
        });
    }

    // Back to the theme the run started under, then the classes that have more widgets than before
    manager->setActiveTheme(QString::fromLatin1(ThemeManager::OriginalName));
    QApplication::processEvents();
    QApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    const QHash<QString, int> widgetsAfter = widgetsByClass();
    QJsonObject grown;
    for (auto count = widgetsAfter.constBegin(); count != widgetsAfter.constEnd(); ++count)
    {
        const int more = count.value() - widgetsBefore.value(count.key());
        if (more != 0)
        {
            grown.insert(count.key(), more);
        }
    }

    QFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        file.write(QJsonDocument(
            QJsonObject{ { "rounds", Rounds }, { "edits", Edits }, { "runs", runs }, { "widgets_grown", grown } }).toJson());
    }
}

int main(int argc, char **argv)
{
    const AZ::Debug::Trace tracer;
    ComponentApplicationWrapper componentApplicationWrapper;

    QApplication::setOrganizationName("O3DE");
    QApplication::setOrganizationDomain("o3de.org");
    QApplication::setApplicationName("O3DEWidgetGallery");

    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);

    qInstallMessageHandler(LogToDebug);

    AzQtComponents::Utilities::HandleDpiAwareness(AzQtComponents::Utilities::PerScreenDpiAware);
    QApplication app(argc, argv);

    AzQtComponents::StyleManager styleManager(&app);
    AZ::IO::FixedMaxPath engineRootPath;
    if (auto settingsRegistry = AZ::SettingsRegistry::Get(); settingsRegistry != nullptr)
    {
        settingsRegistry->Get(engineRootPath.Native(), AZ::SettingsRegistryMergeUtils::FilePathKey_EngineRootFolder);
    }
    styleManager.initialize(&app, engineRootPath);

    // Flattened GUI: the theme the user chose last. The Gallery is where a theme is tried on every control.
    AzQtComponents::ThemeManager::restoreSavedTheme();

    auto wrapper = new AzQtComponents::WindowDecorationWrapper(AzQtComponents::WindowDecorationWrapper::OptionNone);
    auto widget = new ComponentDemoWidget(wrapper);
    wrapper->setGuest(widget);
    widget->resize(550, 900);
    widget->show();

    wrapper->enableSaveRestoreGeometry("windowGeometry");
    wrapper->restoreGeometryFromSettings();

    QObject::connect(widget, &ComponentDemoWidget::refreshStyle, &styleManager, [&styleManager]() {
        styleManager.Refresh();
    });

    app.setQuitOnLastWindowClosed(true);

    // --screenshot-dir <path>: rasterize every gallery page to PNG and exit.
    // Pairs with --disable-stylesheets to produce flattened vs qss-mode sets
    // for side-by-side parity evaluation.
    const QStringList arguments = QApplication::arguments();

    // --page <name-or-index>: start on a specific gallery page
    const int pageArgIndex = arguments.indexOf(QStringLiteral("--page"));
    if (pageArgIndex >= 0 && pageArgIndex + 1 < arguments.size())
    {
        widget->selectPage(arguments.at(pageArgIndex + 1));
    }

    // --theme-editor: start with the theme editor open in its window (flattened GUI)
    if (arguments.contains(QStringLiteral("--theme-editor")) && AzQtComponents::ThemeManager::instance())
    {
        AzQtComponents::ThemeEditor::showWindow(widget);
    }

    // --theme-bench <file>: time changes of theme on this window, write them to the file and exit (flattened GUI)
    const int benchArgIndex = arguments.indexOf(QStringLiteral("--theme-bench"));
    if (benchArgIndex >= 0 && benchArgIndex + 1 < arguments.size() && AzQtComponents::ThemeManager::instance())
    {
        const QString benchFile = arguments.at(benchArgIndex + 1);
        QTimer::singleShot(1500, widget, [benchFile]() {
            RunThemeBench(benchFile);
            QApplication::quit();
        });
    }

    const int screenshotArgIndex = arguments.indexOf(QStringLiteral("--screenshot-dir"));
    if (screenshotArgIndex >= 0 && screenshotArgIndex + 1 < arguments.size())
    {
        const QString screenshotDir = arguments.at(screenshotArgIndex + 1);
        QTimer::singleShot(800, widget, [widget, screenshotDir]() {
            widget->captureAllPages(screenshotDir);
            QApplication::quit();
        });
    }

    app.exec();
}
