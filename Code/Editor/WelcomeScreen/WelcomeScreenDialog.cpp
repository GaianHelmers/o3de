/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include "EditorDefs.h"

#include "WelcomeScreenDialog.h"
#include "WelcomeNewsFeed.h"

// Qt
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QToolTip>
#include <QMenu>
#include <QDesktopServices>
#include <QFileDialog>
#include <QMessageBox>
#include <QScreen>
#include <QTimer>
#include <QDateTime>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QIcon>
#include <QTabWidget>
#include <QTabBar>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFrame>
#include <QHeaderView>
#include <QPainter>
#include <QFont>
#include <QStackedWidget>
#include <QButtonGroup>
#include <QGridLayout>
#include <QLinearGradient>
#include <QScrollArea>
#include <QStyle>
#include <QProcess>
#include <QFileInfo>
#include <QFileIconProvider>
#include <QUrl>

#include <AzCore/Utils/Utils.h>
#include <AzCore/Interface/Interface.h>
#include <AzCore/Settings/SettingsRegistry.h>


// AzToolsFramework
#include <AzToolsFramework/UI/UICore/WidgetHelpers.h>
#include <AzToolsFramework/API/ToolsApplicationAPI.h>   // AzToolsFramework::OpenViewPane

// AzQtComponents
#include <AzQtComponents/Components/Widgets/CheckBox.h>
#include <AzQtComponents/Components/WindowDecorationWrapper.h>
#include <AzQtComponents/Components/Titlebar.h>
#include <AzQtComponents/Components/StyleManagerInterface.h>
#include <AzQtComponents/Utilities/PixmapScaleUtilities.h>

// Editor
#include "Settings.h"
#include "MainWindow.h"
#include "CryEdit.h"
#include "LevelFileDialog.h"
#include "LevelRoots.h"
#include "QtViewPaneManager.h"   // registered view-pane lookup for in-editor tools
#include "LyViewPaneNames.h"

#include <WelcomeScreen/ui_WelcomeScreenDialog.h>

using namespace AzQtComponents;

#define WMSEVENTNAME "WMSEvent"
#define WMSEVENTOPERATION "operation"

// Official O3DE community / resource URLs surfaced by the welcome portal. Centralised here so the
// curated links live in one place. The editor only deep-links to these via QDesktopServices.
namespace
{
    namespace Url
    {
        constexpr const char* Donate      = "https://o3de.org/donate/";
        constexpr const char* Contribute  = "https://o3de.org/contribute/";
        constexpr const char* NewsBlogs   = "https://o3de.org/news-blogs/";
        constexpr const char* Docs        = "https://www.docs.o3de.org/docs/";
        constexpr const char* GitHub      = "https://github.com/o3de/o3de";
        constexpr const char* GitHubOrg   = "https://github.com/o3de";
        constexpr const char* RepoExtras  = "https://github.com/o3de/o3de-extras";
        constexpr const char* RepoThreeP  = "https://github.com/o3de/3p-package-source";
        constexpr const char* RepoDocs    = "https://github.com/o3de/o3de.org";
        constexpr const char* Discussions = "https://github.com/o3de/o3de/discussions";
        constexpr const char* Reddit      = "https://www.reddit.com/r/O3DE/";
        constexpr const char* Discord     = "https://discord.com/invite/o3de";
        constexpr const char* YouTube     = "https://www.youtube.com/channel/UCTC8GDw1XidOTUBEFRbN-sA";
        constexpr const char* Twitter     = "https://twitter.com/o3dengine";
        constexpr const char* LinkedIn    = "https://www.linkedin.com/company/o3de/";
        constexpr const char* Mastodon    = "https://social.lfx.dev/@O3DF";
        constexpr const char* Twitch      = "https://www.twitch.tv/o3de";
        constexpr const char* Spotify     = "https://open.spotify.com/show/1zUZ2crUiWSm7J2P88QMw1";
    }

    void OpenExternalUrl(const QString& url)
    {
        QDesktopServices::openUrl(QUrl(url));
    }

    // A scrollable tab page: returns the QScrollArea, hands back the content's vertical layout.
    QWidget* MakeScrollPage(QVBoxLayout*& outLayout, int margin, int spacing)
    {
        QScrollArea* scroll = new QScrollArea();
        scroll->setObjectName(QStringLiteral("welcomeTabScroll"));
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        QWidget* content = new QWidget();
        content->setObjectName(QStringLiteral("welcomeTabContent"));
        outLayout = new QVBoxLayout(content);
        outLayout->setContentsMargins(margin, margin, margin, margin);
        outLayout->setSpacing(spacing);
        scroll->setWidget(content);
        return scroll;
    }

    // Remove and delete every child item (widgets + spacers) from a layout so it can be repopulated.
    void ClearLayout(QLayout* layout)
    {
        if (!layout)
        {
            return;
        }
        while (QLayoutItem* item = layout->takeAt(0))
        {
            // Delete immediately (not deleteLater): RefreshNewsViews() rebuilds synchronously several
            // times before the event loop runs, so deferred widgets would stack up and double-render.
            delete item->widget();
            delete item;
        }
    }

    // Deterministic gradient per news category, so cards are colourful but a category always looks the
    // same between sessions.
    void GradientForCategory(const QString& category, QColor& c1, QColor& c2)
    {
        static const QColor palette[][2] = {
            { QColor(0x26, 0x80, 0xEB), QColor(0x6A, 0x41, 0xA4) },
            { QColor(0x0E, 0xA5, 0xA4), QColor(0x26, 0x80, 0xEB) },
            { QColor(0x8B, 0x5C, 0xF6), QColor(0xE2, 0x52, 0x43) },
            { QColor(0x43, 0xB5, 0x81), QColor(0x0E, 0xA5, 0xA4) },
            { QColor(0x91, 0x46, 0xFF), QColor(0x37, 0x30, 0xA3) },
            { QColor(0xE2, 0x6D, 0x43), QColor(0xA9, 0x41, 0x6A) },
        };
        const size_t count = sizeof(palette) / sizeof(palette[0]);
        const size_t index = qHash(category) % count;
        c1 = palette[index][0];
        c2 = palette[index][1];
    }
}

static int GetSmallestScreenHeight()
{
    int smallestHeight = -1;
    for (QScreen* screen : QApplication::screens())
    {
        int screenHeight = screen->availableGeometry().height();
        if ((smallestHeight < 0) || (smallestHeight > screenHeight))
        {
            smallestHeight = screenHeight;
        }
    }

    return smallestHeight;
}

WelcomeScreenDialog::WelcomeScreenDialog(QWidget* pParent)
    : QDialog(new WindowDecorationWrapper(WindowDecorationWrapper::OptionAutoAttach | WindowDecorationWrapper::OptionAutoTitleBarButtons, pParent), Qt::Dialog | Qt::CustomizeWindowHint | Qt::WindowCloseButtonHint | Qt::WindowTitleHint)
    , ui(new Ui::WelcomeScreenDialog)
    , m_pRecentList(nullptr)
{
    ui->setupUi(this);

    // Set the project preview image
    QString projectPreviewPath = QDir(AZ::Utils::GetProjectPath().c_str()).filePath("preview.png");
    QFileInfo projectPreviewPathInfo(projectPreviewPath);
    if (!projectPreviewPathInfo.exists() || !projectPreviewPathInfo.isFile())
    {
        projectPreviewPath = ":/WelcomeScreenDialog/DefaultProjectImage.png";
    }

    ui->activeProjectIcon->setPixmap(
        AzQtComponents::ScalePixmapForScreenDpi(
            QPixmap(projectPreviewPath),
            screen(),
            ui->activeProjectIcon->size(),
            Qt::KeepAspectRatioByExpanding,
            Qt::SmoothTransformation
        )
    );

    ui->recentLevelTable->setColumnCount(2);
    ui->recentLevelTable->setMouseTracking(true);
    ui->recentLevelTable->setContextMenuPolicy(Qt::CustomContextMenu);
    ui->recentLevelTable->horizontalHeader()->hide();
    ui->recentLevelTable->verticalHeader()->hide();
    ui->recentLevelTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->recentLevelTable->setSelectionMode(QAbstractItemView::SingleSelection);
    ui->recentLevelTable->setIconSize(QSize(20, 20));
    installEventFilter(this);

    auto projectDisplayName = AZ::Utils::GetProjectDisplayName();
    ui->currentProjectName->setText(projectDisplayName.c_str());

    // Repurpose the window title bar as the brand bar: "[logo] Project - Engine - vVersion".
    ApplyBrandTitleBar();

    ui->newLevelButton->setDefault(true);

    // Hide these buttons until the new functionality is added
    ui->gridButton->hide();
    ui->objectListButton->hide();
    ui->switchProjectButton->hide();

    connect(ui->recentLevelTable, &QWidget::customContextMenuRequested, this, &WelcomeScreenDialog::OnShowContextMenu);

    connect(ui->recentLevelTable, &QTableWidget::entered, this, &WelcomeScreenDialog::OnShowToolTip);
    connect(ui->recentLevelTable, &QTableWidget::clicked, this, &WelcomeScreenDialog::OnRecentLevelTableItemClicked);

    connect(ui->newLevelButton, &QPushButton::clicked, this, &WelcomeScreenDialog::OnNewLevelBtnClicked);
    connect(ui->levelFileLabel, &QLabel::linkActivated, this, &WelcomeScreenDialog::OnNewLevelLabelClicked);
    connect(ui->openLevelButton, &QPushButton::clicked, this, &WelcomeScreenDialog::OnOpenLevelBtnClicked);

    // Wrap the level launcher in the fixed navigation tabs and add the persistent support footer.
    BuildPortalShell();

    // Adjust the height, if need be
    // Do it in the constructor so that the WindowDecoratorWrapper handles it correctly
    int smallestHeight = GetSmallestScreenHeight();
    if (smallestHeight < geometry().height())
    {
        const int SOME_PADDING_IN_PIXELS = 90;
        int difference = geometry().height() - (smallestHeight - SOME_PADDING_IN_PIXELS);

        QRect newGeometry = geometry().adjusted(0, difference / 2, 0, -difference / 2);
        setMinimumSize(minimumSize().width(), newGeometry.height());
        resize(newGeometry.size());
    }

    m_levelExtension = EditorUtils::LevelFile::GetDefaultFileExtension();
}


WelcomeScreenDialog::~WelcomeScreenDialog()
{
    delete ui;
}


//////////////////////////////////////////////////////////////////////////
// Welcome portal shell
//////////////////////////////////////////////////////////////////////////

int WelcomeScreenDialog::ThemeMetric(const char* token, int fallback) const
{
    // The welcome dialog is built after the active theme has loaded, so these reads are live. Falls
    // back to the supplied default when a theme omits the token. Tokens live in the "Welcome Screen"
    // category of the theme files (WelcomeScreen*).
    if (auto* styleManager = AZ::Interface<AzQtComponents::StyleManagerInterface>::Get();
        styleManager && styleManager->IsStylePropertyDefined(token))
    {
        return styleManager->GetStylePropertyAsInteger(token);
    }
    return fallback;
}

void WelcomeScreenDialog::ApplyBrandTitleBar()
{
    // Brand string: "<Project> - <Engine> - v<Version>". Engine name + version come from the registered
    // engine manifest (settings registry); each part is optional and falls back gracefully.
    QString brand = AZ::Utils::GetProjectDisplayName().c_str();

    AZStd::string engineName;
    AZStd::string engineVersion;
    if (auto* registry = AZ::SettingsRegistry::Get())
    {
        registry->Get(engineName, "/O3DE/Runtime/Manifest/Engine/engine_name");
        registry->Get(engineVersion, "/O3DE/Runtime/Manifest/Engine/version");
    }
    // Middle-dot separators, built via QChar so the source file stays ASCII (no literal unicode bytes).
    const QString brandSeparator = QStringLiteral("   ") + QChar(0x00B7) + QStringLiteral("   ");
    if (!engineName.empty())
    {
        brand += brandSeparator + QString::fromUtf8(engineName.c_str());
    }
    if (!engineVersion.empty())
    {
        brand += brandSeparator + QStringLiteral("v") + QString::fromUtf8(engineVersion.c_str());
    }

    // The dialog is hosted in a WindowDecorationWrapper (its parent). Repurpose that title bar as the
    // brand bar: the O3DE logo as the icon plus the brand text, keeping the window controls.
    setWindowTitle(brand);
    if (auto* wrapper = qobject_cast<AzQtComponents::WindowDecorationWrapper*>(parentWidget()))
    {
        if (auto* titleBar = wrapper->titleBar())
        {
            // Bake equal transparent padding into the logo pixmap so the title bar does not stretch the
            // mark edge-to-edge (the bar scales the icon to its height); equal pad keeps it centered.
            const int glyph = 16;
            const int pad = 8;
            QPixmap logo(glyph + 2 * pad, glyph + 2 * pad);
            logo.fill(Qt::transparent);
            {
                QPainter painter(&logo);
                QIcon(QStringLiteral(":/StartupLogoDialog/o3de_icon.svg")).paint(&painter, pad, pad, glyph, glyph);
            }
            titleBar->setIcon(logo);
            titleBar->setWindowTitleOverride(brand);
        }
    }
}

void WelcomeScreenDialog::ApplyWelcomeStyle()
{
    // Bespoke welcome-screen styling, built from the theme tokens (reuse) + the WelcomeScreen* category.
    // Applied to the dialog so it merges with the global themed stylesheet (object-name keyed rules win).
    auto themeColor = [](const char* token, const char* fallback) -> QString
    {
        if (auto* sm = AZ::Interface<AzQtComponents::StyleManagerInterface>::Get();
            sm && sm->IsStylePropertyDefined(token))
        {
            const QColor c = sm->GetStylePropertyAsColor(token);
            if (c.isValid())
            {
                return c.name();
            }
        }
        return QString::fromLatin1(fallback);
    };

    const QString panel       = themeColor("PanelBackgroundColor", "#1F2129");
    const QString dark        = themeColor("DarkPanelBackgroundColor", "#15161D");
    const QString card        = themeColor("CardBackgroundColor", "#32353F");
    const QString cardHover   = themeColor("WelcomeScreenCardHoverColor", "#2F333D");
    const QString border      = themeColor("InputBorderColor", "#3C3F4A");
    const QString accent      = themeColor("MenuItemSelectedColor", "#2680EB");
    const QString accentHi    = themeColor("LinkColor", "#5B9CFF");
    const QString textPrimary = themeColor("PrimaryTextColor", "#FFFFFF");
    const QString textBody    = themeColor("WindowTextColor", "#C8CDD6");
    const QString muted       = themeColor("SecondaryTextColor", "#8A92A0");
    const int radius = ThemeMetric("WelcomeScreenRadius", 12);

    QString qss;
    qss += QStringLiteral("#welcomeHero{background:%1;border:1px solid %2;border-radius:%3px;}").arg(panel, border).arg(radius);
    qss += QStringLiteral("#welcomeHeroEyebrow{color:%1;font-weight:700;font-size:11px;}").arg(accentHi);
    qss += QStringLiteral("#welcomeHeroTitle{color:%1;font-weight:800;font-size:24px;}").arg(textPrimary);
    qss += QStringLiteral("#welcomeHeroMeta{color:%1;font-size:13px;}").arg(muted);
    qss += QStringLiteral("#welcomeSectionLabel{color:%1;font-weight:700;font-size:13px;}").arg(muted);
    // Bespoke buttons (styled QLabels, not QPushButtons). Padding gives them their full height; the
    // label centers its own text, so nothing clips.
    qss += QStringLiteral("#welcomePrimaryButton{background:%1;color:#FFFFFF;border:none;border-radius:8px;padding:15px 20px;font-size:14px;font-weight:700;}").arg(accent);
    qss += QStringLiteral("#welcomePrimaryButton:hover{background:%1;}").arg(accentHi);
    qss += QStringLiteral("#welcomeSecondaryButton{background:transparent;color:%1;border:1px solid %2;border-radius:8px;padding:15px 18px;font-weight:600;}").arg(textBody, border);
    qss += QStringLiteral("#welcomeSecondaryButton:hover{background:%1;color:%2;}").arg(cardHover, textPrimary);
    qss += QStringLiteral("#welcomeQuickAction{background:transparent;border:none;border-radius:7px;}");
    qss += QStringLiteral("#welcomeQuickAction:hover{background:%1;}").arg(cardHover);
    qss += QStringLiteral("#welcomeQuickActionText{color:%1;font-weight:600;font-size:14px;}").arg(textBody);
    qss += QStringLiteral("#welcomeQuickAction:hover #welcomeQuickActionText{color:%1;}").arg(textPrimary);
    qss += QStringLiteral("#welcomeQuickActionSeparator{background:%1;max-height:1px;border:none;}").arg(border);
    qss += QStringLiteral("#welcomeSupportFooter{background:%1;border-top:1px solid %2;}").arg(dark, border);
    qss += QStringLiteral("#welcomeFooterMessage{color:%1;}").arg(muted);
    qss += QStringLiteral("#welcomeSupportButton{background:transparent;color:%1;border:none;font-weight:700;}").arg(accentHi);
    qss += QStringLiteral("#welcomeSupportButton:hover{color:%1;}").arg(textPrimary);

    // Bespoke tabs (styled QLabels). Active state driven by the [tabActive] property (QLabels have no
    // :checked). The label centers its own text -> never bottom-aligned/clipped like a QPushButton.
    qss += QStringLiteral("#welcomeTabBar{background:%1;border-bottom:1px solid %2;}").arg(panel, border);
    qss += QStringLiteral("#welcomeTab{background:transparent;color:%1;border-bottom:3px solid transparent;padding:13px 22px;font-weight:600;font-size:15px;border-top-left-radius:6px;border-top-right-radius:6px;}").arg(muted);
    qss += QStringLiteral("#welcomeTab:hover{color:%1;background:%2;}").arg(textBody, cardHover);
    qss += QStringLiteral("#welcomeTab[tabActive=\"true\"]{color:%1;background:%2;border-bottom:3px solid %3;font-weight:800;}").arg(textPrimary, card, accent);
    qss += QStringLiteral("#welcomeTabScroll,#welcomeTabContent{background:transparent;border:none;}");

    // Content cards (Community / Resources / Contribute / News).
    qss += QStringLiteral("#welcomeCard{background:%1;border:1px solid %2;border-radius:%3px;}").arg(card, border).arg(radius);
    qss += QStringLiteral("#welcomeCard:hover{background:%1;border-color:%2;}").arg(cardHover, accent);
    qss += QStringLiteral("#welcomeCardTitle{color:%1;font-weight:700;font-size:14px;}").arg(textPrimary);
    qss += QStringLiteral("#welcomeCardBody{color:%1;font-size:12px;}").arg(muted);
    qss += QStringLiteral("#welcomeCardLink{color:%1;font-weight:700;font-size:12px;}").arg(accentHi);
    qss += QStringLiteral("#welcomeCardChip{background:rgba(0,0,0,140);color:#FFFFFF;font-weight:700;font-size:10px;padding:3px 8px;border-radius:5px;}");
    qss += QStringLiteral("#welcomeFeedStatus{color:%1;font-size:11px;}").arg(muted);
    qss += QStringLiteral("#welcomeNewsRow{background:transparent;border:none;border-radius:8px;}");
    qss += QStringLiteral("#welcomeNewsRow:hover{background:%1;}").arg(cardHover);
    qss += QStringLiteral("#welcomeNewsRowTitle{color:%1;font-weight:700;font-size:14px;}").arg(textPrimary);
    qss += QStringLiteral("#welcomeNewsRowDate{color:%1;font-size:12px;}").arg(muted);
    qss += QStringLiteral("#welcomeDonatePanel{background:%1;border:1px solid %2;border-radius:%3px;}").arg(panel, border).arg(radius);
    qss += QStringLiteral("#welcomeDonateButton{background:%1;color:#08240F;border:none;border-radius:8px;padding:11px 22px;font-weight:700;}").arg(themeColor("TextPositiveColor", "#43D96A"));

    // Bespoke recent-level tiles (whole tile is the hover/click target -- name + date as one element).
    qss += QStringLiteral("#welcomeLevelTile{background:%1;border:1px solid %2;border-radius:8px;}").arg(card, border);
    qss += QStringLiteral("#welcomeLevelTile:hover{background:%1;border-color:%2;}").arg(cardHover, accent);
    qss += QStringLiteral("#welcomeLevelTileName{color:%1;font-weight:600;font-size:13px;}").arg(textPrimary);
    qss += QStringLiteral("#welcomeLevelTileDate{color:%1;font-size:11px;}").arg(muted);

    setStyleSheet(qss);
}

void WelcomeScreenDialog::BuildPortalShell()
{
    // Bespoke nav: a flat tab bar (accent underline on the active tab) over a stacked body. Built by
    // hand rather than QTabWidget so the styling matches the design exactly.
    m_navStack = new QStackedWidget(this);

    QWidget* tabBar = new QWidget(this);
    tabBar->setObjectName(QStringLiteral("welcomeTabBar"));
    QHBoxLayout* tabBarLayout = new QHBoxLayout(tabBar);
    tabBarLayout->setContentsMargins(24, 0, 24, 0);
    tabBarLayout->setSpacing(8);

    // Tabs are bespoke QLabels (NOT QPushButtons -- O3DE custom-paints those in code, which clips them
    // and ignores our QSS). A QLabel honours our padding/background/font exactly.
    auto addTab = [&](const QString& label, QWidget* page) -> int
    {
        const int index = m_navStack->addWidget(page);
        QLabel* tab = new QLabel(label, tabBar);
        tab->setObjectName(QStringLiteral("welcomeTab"));
        tab->setAlignment(Qt::AlignCenter);
        tab->setAttribute(Qt::WA_StyledBackground, true);
        tab->setAttribute(Qt::WA_Hover, true);
        tab->setCursor(Qt::PointingHandCursor);
        tab->setProperty("tabIndex", index);
        tab->setProperty("tabActive", index == 0);
        tab->installEventFilter(this);
        tabBarLayout->addWidget(tab);
        m_tabLabels.append(tab);
        return index;
    };

    // The Project page is rebuilt as the hero portal (it reparents the preview / project name / recent
    // tiles out of the .ui's 2-column body), so the old container is removed from view.
    addTab(tr("Project"), BuildProjectPage());
    ui->verticalLayout->removeWidget(ui->projectPageContainer);
    ui->projectPageContainer->hide();

    addTab(tr("News"), CreateNewsPage());
    m_communityTabIndex = addTab(tr("Community"), CreateCommunityPage());
    addTab(tr("Resources"), CreateResourcesPage());
    addTab(tr("Contribute"), CreateContributePage());
    tabBarLayout->addStretch();

    // Persistent, understated support footer shown beneath every tab.
    QWidget* footer = CreateSupportFooter();

    ui->verticalLayout->setSpacing(0);
    ui->verticalLayout->setContentsMargins(0, 0, 0, 0);
    ui->verticalLayout->addWidget(tabBar, 0);
    ui->verticalLayout->addWidget(m_navStack, 1);
    ui->verticalLayout->addWidget(footer, 0);

    // Bespoke welcome styling now that all object-named widgets exist.
    ApplyWelcomeStyle();

    // Live news: show any disk cache immediately, then refresh from o3de.org asynchronously.
    m_newsFeed = new O3DEWelcome::WelcomeNewsFeed(this);
    connect(m_newsFeed, &O3DEWelcome::WelcomeNewsFeed::Updated, this, &WelcomeScreenDialog::RefreshNewsViews);
    RefreshNewsViews();
    m_newsFeed->Refresh();
}

QWidget* WelcomeScreenDialog::BuildProjectPage()
{
    const int pageMargin = ThemeMetric("WelcomeScreenPageMargin", 24);
    const int sectionSpacing = ThemeMetric("WelcomeScreenSectionSpacing", 12);
    const int heroPad = ThemeMetric("WelcomeScreenCardPadding", 16);

    QWidget* page = new QWidget(this);
    QVBoxLayout* pageLayout = new QVBoxLayout(page);
    pageLayout->setContentsMargins(pageMargin, pageMargin, pageMargin, pageMargin);
    pageLayout->setSpacing(sectionSpacing);

    // ---- Hero: preview (left) | welcome text | quick actions ----
    QWidget* hero = new QWidget(page);
    hero->setObjectName(QStringLiteral("welcomeHero"));
    QHBoxLayout* heroLayout = new QHBoxLayout(hero);
    heroLayout->setContentsMargins(heroPad, heroPad, heroPad + 12, heroPad);   // extra right relief for quick actions
    heroLayout->setSpacing(24);

    // Never let the hero be vertically compressed below its content -- that is what punched the CTA
    // through the bottom frame.
    hero->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);

    // The preview fills the hero height and frames the text. Crucially, the CTA is grouped right under
    // the project meta with ALL remaining slack placed BELOW it -- so the button is pulled up and can
    // never sit on, or overflow, the bottom edge.
    ui->activeProjectIcon->setMinimumSize(0, 0);
    ui->activeProjectIcon->setMaximumSize(16777215, 16777215);
    ui->activeProjectIcon->setFixedWidth(150);
    ui->activeProjectIcon->setMinimumHeight(180);
    ui->activeProjectIcon->setScaledContents(true);
    ui->activeProjectIcon->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Ignored);  // fill height, ignore the pixmap's own size hint
    heroLayout->addWidget(ui->activeProjectIcon, 0);

    QWidget* htext = new QWidget(hero);
    QVBoxLayout* htextLayout = new QVBoxLayout(htext);
    htextLayout->setContentsMargins(0, 0, 0, 0);
    htextLayout->setSpacing(6);
    m_heroEyebrow = new QLabel(htext);
    m_heroEyebrow->setObjectName(QStringLiteral("welcomeHeroEyebrow"));
    ui->currentProjectName->setObjectName(QStringLiteral("welcomeHeroTitle"));
    m_heroMeta = new QLabel(htext);
    m_heroMeta->setObjectName(QStringLiteral("welcomeHeroMeta"));
    // Project path: useful context (which project, where), and not redundant with the Resume button.
    const auto projectPath = AZ::Utils::GetProjectPath();
    m_heroMeta->setText(QString::fromUtf8(projectPath.c_str()));
    m_heroCtaContainer = new QWidget(htext);
    QVBoxLayout* ctaLayout = new QVBoxLayout(m_heroCtaContainer);
    ctaLayout->setContentsMargins(0, 0, 0, 0);

    // Eyebrow/title/path anchored to the TOP (aligns with the preview top); the CTA anchored to the
    // BOTTOM (aligns with the preview bottom + hero frame). Safe now that the CTA is a bespoke QLabel
    // that holds its full height and never clips.
    htextLayout->addWidget(m_heroEyebrow);
    htextLayout->addWidget(ui->currentProjectName);
    htextLayout->addWidget(m_heroMeta);
    htextLayout->addStretch(1);
    htextLayout->addWidget(m_heroCtaContainer);
    heroLayout->addWidget(htext, 1);

    QWidget* quickActions = BuildQuickActions(hero);
    quickActions->setMinimumWidth(230);     // a generous ~1/4 so the actions are not crushed
    heroLayout->addWidget(quickActions, 0, Qt::AlignTop);
    pageLayout->addWidget(hero, 0);

    // ---- Recent levels (left) + latest updates rail (right) ----
    QWidget* row = new QWidget(page);
    QHBoxLayout* rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(0, 0, 0, 0);
    rowLayout->setSpacing(20);

    QWidget* recentCol = new QWidget(row);
    QVBoxLayout* recentLayout = new QVBoxLayout(recentCol);
    recentLayout->setContentsMargins(0, 0, 0, 0);
    QLabel* recentLabel = new QLabel(tr("RECENT LEVELS"), recentCol);
    recentLabel->setObjectName(QStringLiteral("welcomeSectionLabel"));
    recentLayout->addWidget(recentLabel);
    // Bespoke clickable level tiles (built in SetRecentFileList) instead of a QTableWidget.
    QWidget* recentList = new QWidget(recentCol);
    m_recentLevelsLayout = new QVBoxLayout(recentList);
    m_recentLevelsLayout->setContentsMargins(0, 0, 0, 0);
    m_recentLevelsLayout->setSpacing(8);
    recentLayout->addWidget(recentList);
    recentLayout->addStretch();   // pin content to the top (matches the rail column)
    rowLayout->addWidget(recentCol, 3);

    QWidget* railCol = new QWidget(row);
    QVBoxLayout* railLayout = new QVBoxLayout(railCol);
    railLayout->setContentsMargins(0, 0, 0, 0);
    m_railLabel = new QLabel(tr("PROJECT TOOLS"), railCol);
    m_railLabel->setObjectName(QStringLiteral("welcomeSectionLabel"));
    railLayout->addWidget(m_railLabel);
    // Launchers for the standalone tools shipped next to the Editor (Material Editor, Asset Processor...).
    m_railFeedLayout = new QVBoxLayout();
    m_railFeedLayout->setContentsMargins(0, 0, 0, 0);
    m_railFeedLayout->setSpacing(10);   // clear gap so each tool reads as its own sliver
    railLayout->addLayout(m_railFeedLayout);
    PopulateProjectTools();
    railLayout->addStretch();
    rowLayout->addWidget(railCol, 2);

    pageLayout->addWidget(row, 0);
    pageLayout->addStretch(1);    // natural content height; extra space stays at the bottom

    UpdateHeroState(QString());   // first-run default until SetRecentFileList provides the last level

    // Make the whole Project page scroll, so a generous tool list / many recent levels never clip on
    // smaller windows.
    QScrollArea* scroll = new QScrollArea(this);
    scroll->setObjectName(QStringLiteral("welcomeTabScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setWidget(page);
    return scroll;
}

QWidget* WelcomeScreenDialog::BuildQuickActions(QWidget* parent)
{
    QWidget* qa = new QWidget(parent);
    qa->setObjectName(QStringLiteral("welcomeQuickActions"));
    QVBoxLayout* layout = new QVBoxLayout(qa);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);

    QLabel* title = new QLabel(tr("QUICK ACTIONS"), qa);
    title->setObjectName(QStringLiteral("welcomeSectionLabel"));
    layout->addWidget(title);
    layout->addSpacing(8);

#if defined(AZ_PLATFORM_WINDOWS)
    const QString releasesUrl = QStringLiteral("https://o3debinaries.org/download/windows.html");
#elif defined(AZ_PLATFORM_LINUX)
    const QString releasesUrl = QStringLiteral("https://o3debinaries.org/download/linux.html");
#else
    const QString releasesUrl = QStringLiteral("https://o3de.org/download/");
#endif

    // Bespoke icon + label rows (no QPushButton). Click is dispatched through eventFilter by scheme.
    layout->addWidget(MakeQuickAction(tr("New level"), QStyle::SP_FileIcon, QStringLiteral("action:new")));
    layout->addWidget(MakeQuickAction(tr("Open level"), QStyle::SP_DirOpenIcon, QStringLiteral("action:open")));

    layout->addSpacing(8);
    QFrame* sep = new QFrame(qa);
    sep->setObjectName(QStringLiteral("welcomeQuickActionSeparator"));
    sep->setFrameShape(QFrame::HLine);
    layout->addWidget(sep);
    layout->addSpacing(8);

    layout->addWidget(MakeQuickAction(tr("Engine Versions"), QStyle::SP_ArrowDown, releasesUrl));
    layout->addWidget(MakeQuickAction(tr("Need help?"), QStyle::SP_MessageBoxQuestion, QStringLiteral("action:community")));

    layout->addStretch();   // keep the actions pinned to the top of the column
    return qa;
}

QWidget* WelcomeScreenDialog::BuildLevelTile(const QString& name, const QString& dateText, int index)
{
    QWidget* tile = new QWidget();
    tile->setObjectName(QStringLiteral("welcomeLevelTile"));
    tile->setAttribute(Qt::WA_StyledBackground, true);
    tile->setAttribute(Qt::WA_Hover, true);
    tile->setProperty("welcomeLevelIndex", index);
    tile->setCursor(Qt::PointingHandCursor);
    tile->installEventFilter(this);

    QHBoxLayout* layout = new QHBoxLayout(tile);
    layout->setContentsMargins(12, 10, 12, 10);
    layout->setSpacing(12);

    QLabel* icon = new QLabel(tile);
    const QPixmap levelIcon = QIcon(QStringLiteral(":/Level/level.svg")).pixmap(QSize(22, 22));
    if (!levelIcon.isNull())
    {
        icon->setPixmap(levelIcon);
    }
    icon->setFixedSize(24, 24);
    layout->addWidget(icon, 0);

    QWidget* textCol = new QWidget(tile);
    QVBoxLayout* textLayout = new QVBoxLayout(textCol);
    textLayout->setContentsMargins(0, 0, 0, 0);
    textLayout->setSpacing(1);
    QLabel* nameLabel = new QLabel(name, textCol);
    nameLabel->setObjectName(QStringLiteral("welcomeLevelTileName"));
    QLabel* dateLabel = new QLabel(dateText, textCol);
    dateLabel->setObjectName(QStringLiteral("welcomeLevelTileDate"));
    textLayout->addWidget(nameLabel);
    textLayout->addWidget(dateLabel);
    layout->addWidget(textCol, 1);

    return tile;
}

void WelcomeScreenDialog::UpdateHeroState(const QString& lastLevelName)
{
    if (!m_heroCtaContainer || !m_heroEyebrow || !m_heroMeta)
    {
        return;
    }

    // Clear the previous CTA widgets.
    QLayout* ctaLayout = m_heroCtaContainer->layout();
    while (QLayoutItem* item = ctaLayout->takeAt(0))
    {
        delete item->widget();
        delete item;
    }

    if (!lastLevelName.isEmpty())
    {
        // Resume state -- bespoke button (a QLabel), so it sizes to its content and never clips.
        m_heroEyebrow->setText(tr("WELCOME BACK"));
        ctaLayout->addWidget(MakeBespokeButton(tr("Resume %1").arg(lastLevelName),
            QStringLiteral("welcomePrimaryButton"), QStringLiteral("action:resume")));
    }
    else
    {
        // First-run state: two equal CTAs.
        m_heroEyebrow->setText(tr("GET STARTED"));
        QWidget* pair = new QWidget(m_heroCtaContainer);
        QHBoxLayout* pairLayout = new QHBoxLayout(pair);
        pairLayout->setContentsMargins(0, 0, 0, 0);
        pairLayout->setSpacing(10);
        pairLayout->addWidget(MakeBespokeButton(tr("Create a level"), QStringLiteral("welcomePrimaryButton"), QStringLiteral("action:new")));
        pairLayout->addWidget(MakeBespokeButton(tr("Open a level"), QStringLiteral("welcomeSecondaryButton"), QStringLiteral("action:open")));
        ctaLayout->addWidget(pair);
    }
}

QLabel* WelcomeScreenDialog::MakeBespokeButton(const QString& text, const QString& objectName, const QString& clickUrl)
{
    // A push-button-styled QLabel. O3DE custom-paints real QPushButtons in code (clipping them and
    // ignoring our QSS), so we use a styled QLabel that honours padding/background/font exactly.
    QLabel* button = new QLabel(text);
    button->setObjectName(objectName);
    button->setAlignment(Qt::AlignCenter);
    button->setAttribute(Qt::WA_StyledBackground, true);
    button->setAttribute(Qt::WA_Hover, true);
    button->setCursor(Qt::PointingHandCursor);
    button->setProperty("welcomeCardUrl", clickUrl);
    button->installEventFilter(this);
    return button;
}

QWidget* WelcomeScreenDialog::MakeQuickAction(const QString& text, QStyle::StandardPixmap icon, const QString& clickUrl)
{
    // Bespoke icon + label row (matches the PROJECT TOOLS rows), immune to the QPushButton painter.
    QWidget* row = new QWidget();
    row->setObjectName(QStringLiteral("welcomeQuickAction"));
    row->setAttribute(Qt::WA_StyledBackground, true);
    row->setAttribute(Qt::WA_Hover, true);
    row->setCursor(Qt::PointingHandCursor);
    row->setProperty("welcomeCardUrl", clickUrl);
    row->installEventFilter(this);

    QHBoxLayout* layout = new QHBoxLayout(row);
    layout->setContentsMargins(10, 9, 10, 9);
    layout->setSpacing(12);
    QLabel* iconLabel = new QLabel(row);
    iconLabel->setFixedSize(18, 18);
    iconLabel->setPixmap(style()->standardIcon(icon).pixmap(18, 18));
    layout->addWidget(iconLabel, 0, Qt::AlignVCenter);
    QLabel* textLabel = new QLabel(text, row);
    textLabel->setObjectName(QStringLiteral("welcomeQuickActionText"));
    layout->addWidget(textLabel, 1);
    return row;
}

void WelcomeScreenDialog::SetActiveTab(int index)
{
    if (index < 0 || !m_navStack)
    {
        return;
    }
    m_navStack->setCurrentIndex(index);
    for (QLabel* tab : m_tabLabels)
    {
        const bool active = tab->property("tabIndex").toInt() == index;
        tab->setProperty("tabActive", active);
        tab->style()->unpolish(tab);
        tab->style()->polish(tab);
    }
}

void WelcomeScreenDialog::OnResumeClicked()
{
    if (!m_levels.empty())
    {
        m_levelPath = m_levels.front().second;
        accept();
    }
}

//////////////////////////////////////////////////////////////////////////
// Reusable bespoke card components
//////////////////////////////////////////////////////////////////////////

QPixmap WelcomeScreenDialog::MakeGradientPixmap(const QColor& c1, const QColor& c2, const QString& monogram, const QSize& size)
{
    QPixmap pixmap(size);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    QLinearGradient gradient(0, 0, size.width(), size.height());
    gradient.setColorAt(0.0, c1);
    gradient.setColorAt(1.0, c2);
    painter.fillRect(QRect(QPoint(0, 0), size), gradient);
    if (!monogram.isEmpty())
    {
        painter.setPen(QColor(255, 255, 255, 235));
        QFont font = painter.font();
        font.setBold(true);
        font.setPixelSize(size.height() / 3);
        painter.setFont(font);
        painter.drawText(QRect(QPoint(0, 0), size), Qt::AlignCenter, monogram);
    }
    return pixmap;
}

QWidget* WelcomeScreenDialog::CreateSectionLabel(const QString& text)
{
    QLabel* label = new QLabel(text);
    label->setObjectName(QStringLiteral("welcomeSectionLabel"));
    return label;
}

QWidget* WelcomeScreenDialog::CreateInfoCard(const QColor& iconColor, const QString& iconGlyph,
    const QString& title, const QString& body, const QString& linkText, const QString& url)
{
    QWidget* card = new QWidget();
    card->setObjectName(QStringLiteral("welcomeCard"));
    card->setAttribute(Qt::WA_StyledBackground, true);
    card->setAttribute(Qt::WA_Hover, true);
    if (!url.isEmpty())
    {
        card->setProperty("welcomeCardUrl", url);
        card->setCursor(Qt::PointingHandCursor);
        card->installEventFilter(this);
    }

    QVBoxLayout* layout = new QVBoxLayout(card);
    layout->setContentsMargins(18, 18, 18, 18);
    layout->setSpacing(8);

    QLabel* icon = new QLabel(iconGlyph, card);
    icon->setAlignment(Qt::AlignCenter);
    icon->setFixedSize(42, 42);
    icon->setStyleSheet(QStringLiteral("background:%1;border-radius:11px;color:#FFFFFF;font-weight:800;font-size:15px;").arg(iconColor.name()));
    layout->addWidget(icon);

    QLabel* titleLabel = new QLabel(title, card);
    titleLabel->setObjectName(QStringLiteral("welcomeCardTitle"));
    layout->addWidget(titleLabel);

    QLabel* bodyLabel = new QLabel(body, card);
    bodyLabel->setObjectName(QStringLiteral("welcomeCardBody"));
    bodyLabel->setWordWrap(true);
    layout->addWidget(bodyLabel);

    if (!linkText.isEmpty())
    {
        QLabel* link = new QLabel(linkText, card);
        link->setObjectName(QStringLiteral("welcomeCardLink"));
        layout->addWidget(link);
    }
    layout->addStretch();
    return card;
}

QWidget* WelcomeScreenDialog::CreateMediaCard(const QString& chip, const QString& title,
    const QString& subtitle, const QColor& c1, const QColor& c2, const QString& url)
{
    QWidget* card = new QWidget();
    card->setObjectName(QStringLiteral("welcomeCard"));
    card->setAttribute(Qt::WA_StyledBackground, true);
    card->setAttribute(Qt::WA_Hover, true);
    if (!url.isEmpty())
    {
        card->setProperty("welcomeCardUrl", url);
        card->setCursor(Qt::PointingHandCursor);
        card->installEventFilter(this);
    }

    QVBoxLayout* layout = new QVBoxLayout(card);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    QLabel* thumb = new QLabel(card);
    thumb->setObjectName(QStringLiteral("welcomeCardThumb"));
    thumb->setFixedHeight(118);
    thumb->setScaledContents(true);
    thumb->setPixmap(MakeGradientPixmap(c1, c2, QString(), QSize(320, 160)));
    if (!chip.isEmpty())
    {
        QLabel* chipLabel = new QLabel(chip, thumb);
        chipLabel->setObjectName(QStringLiteral("welcomeCardChip"));
        chipLabel->move(12, 12);
    }
    layout->addWidget(thumb);

    QWidget* bodyWrap = new QWidget(card);
    QVBoxLayout* bodyLayout = new QVBoxLayout(bodyWrap);
    bodyLayout->setContentsMargins(15, 13, 15, 15);
    bodyLayout->setSpacing(4);
    QLabel* titleLabel = new QLabel(title, bodyWrap);
    titleLabel->setObjectName(QStringLiteral("welcomeCardTitle"));
    titleLabel->setWordWrap(true);
    bodyLayout->addWidget(titleLabel);
    if (!subtitle.isEmpty())
    {
        QLabel* sub = new QLabel(subtitle, bodyWrap);
        sub->setObjectName(QStringLiteral("welcomeCardBody"));
        bodyLayout->addWidget(sub);
    }
    layout->addWidget(bodyWrap);
    return card;
}

QWidget* WelcomeScreenDialog::CreateSocialTile(const QString& name, const QString& handle,
    const QColor& brand, const QString& url)
{
    QWidget* tile = new QWidget();
    tile->setObjectName(QStringLiteral("welcomeCard"));
    tile->setAttribute(Qt::WA_StyledBackground, true);
    tile->setAttribute(Qt::WA_Hover, true);
    if (!url.isEmpty())
    {
        tile->setProperty("welcomeCardUrl", url);
        tile->setCursor(Qt::PointingHandCursor);
        tile->installEventFilter(this);
    }

    QHBoxLayout* layout = new QHBoxLayout(tile);
    layout->setContentsMargins(15, 12, 15, 12);
    layout->setSpacing(12);

    QLabel* icon = new QLabel(name.left(1).toUpper(), tile);
    icon->setAlignment(Qt::AlignCenter);
    icon->setFixedSize(34, 34);
    icon->setStyleSheet(QStringLiteral("background:%1;border-radius:9px;color:#FFFFFF;font-weight:800;").arg(brand.name()));
    layout->addWidget(icon);

    QWidget* textCol = new QWidget(tile);
    QVBoxLayout* textLayout = new QVBoxLayout(textCol);
    textLayout->setContentsMargins(0, 0, 0, 0);
    textLayout->setSpacing(1);
    QLabel* nameLabel = new QLabel(name, textCol);
    nameLabel->setObjectName(QStringLiteral("welcomeCardTitle"));
    QLabel* handleLabel = new QLabel(handle, textCol);
    handleLabel->setObjectName(QStringLiteral("welcomeCardBody"));
    textLayout->addWidget(nameLabel);
    textLayout->addWidget(handleLabel);
    layout->addWidget(textCol, 1);
    return tile;
}

QWidget* WelcomeScreenDialog::CreateCompactNewsRow(const QString& title, const QString& subtitle,
    const QColor& accent, const QString& url)
{
    QWidget* row = new QWidget();
    row->setObjectName(QStringLiteral("welcomeNewsRow"));
    row->setAttribute(Qt::WA_StyledBackground, true);
    row->setAttribute(Qt::WA_Hover, true);
    if (!url.isEmpty())
    {
        row->setProperty("welcomeCardUrl", url);
        row->setCursor(Qt::PointingHandCursor);
        row->installEventFilter(this);
    }

    QHBoxLayout* layout = new QHBoxLayout(row);
    layout->setContentsMargins(12, 9, 12, 9);
    layout->setSpacing(10);

    // Small accent dot instead of a full-bleed gradient thumbnail.
    QLabel* dot = new QLabel(row);
    dot->setFixedSize(8, 8);
    dot->setContentsMargins(0, 4, 0, 0);
    dot->setStyleSheet(QStringLiteral("background:%1;border-radius:4px;").arg(accent.name()));
    layout->addWidget(dot, 0, Qt::AlignTop);

    QWidget* textCol = new QWidget(row);
    QVBoxLayout* textLayout = new QVBoxLayout(textCol);
    textLayout->setContentsMargins(0, 0, 0, 0);
    textLayout->setSpacing(2);
    QLabel* titleLabel = new QLabel(title, textCol);
    titleLabel->setObjectName(QStringLiteral("welcomeNewsRowTitle"));
    titleLabel->setWordWrap(true);
    textLayout->addWidget(titleLabel);
    if (!subtitle.isEmpty())
    {
        QLabel* subLabel = new QLabel(subtitle, textCol);
        subLabel->setObjectName(QStringLiteral("welcomeNewsRowDate"));
        textLayout->addWidget(subLabel);
    }
    layout->addWidget(textCol, 1);
    return row;
}

//////////////////////////////////////////////////////////////////////////
// Tab bodies
//////////////////////////////////////////////////////////////////////////

QWidget* WelcomeScreenDialog::CreateNewsPage()
{
    // Header + status line + a grid host that RefreshNewsViews() fills from the live o3de.org feed
    // (or local quick-start cards when offline).
    const int pageMargin = ThemeMetric("WelcomeScreenPageMargin", 24);
    const int sectionSpacing = ThemeMetric("WelcomeScreenSectionSpacing", 12);
    QVBoxLayout* layout = nullptr;
    QWidget* page = MakeScrollPage(layout, pageMargin, sectionSpacing);

    layout->addWidget(CreateSectionLabel(tr("NEWS AND BLOGS")));
    m_newsStatusLabel = new QLabel(tr("Loading the latest from o3de.org..."));
    m_newsStatusLabel->setObjectName(QStringLiteral("welcomeFeedStatus"));
    layout->addWidget(m_newsStatusLabel);

    m_newsGrid = new QGridLayout();
    m_newsGrid->setSpacing(16);
    layout->addLayout(m_newsGrid);
    layout->addStretch();
    return page;
}

void WelcomeScreenDialog::RefreshNewsViews()
{
    if (!m_newsFeed)
    {
        return;
    }

    using State = O3DEWelcome::WelcomeNewsFeed::State;
    const State state = m_newsFeed->GetState();
    const QVector<O3DEWelcome::NewsArticle>& articles = m_newsFeed->Articles();
    const bool haveArticles = !articles.isEmpty();

    // Status line under the News header.
    if (m_newsStatusLabel)
    {
        QString status;
        switch (state)
        {
        case State::Loading: status = haveArticles ? tr("Refreshing from o3de.org...") : tr("Loading the latest from o3de.org..."); break;
        case State::Online:  status = tr("Live from o3de.org"); break;
        case State::Cached:  status = tr("Showing saved news - reconnect for the latest."); break;
        case State::Offline: status = tr("Couldn't reach o3de.org - showing O3DE resources instead."); break;
        default:             status = haveArticles ? tr("Live from o3de.org") : QString(); break;
        }
        m_newsStatusLabel->setText(status);
    }

    // News tab grid (3 wide).
    if (m_newsGrid)
    {
        ClearLayout(m_newsGrid);
        if (haveArticles)
        {
            int i = 0;
            for (const O3DEWelcome::NewsArticle& article : articles)
            {
                QColor c1, c2;
                GradientForCategory(article.m_category, c1, c2);
                const QString chip = article.m_category.isEmpty() ? tr("Blog") : article.m_category;
                QString subtitle = article.m_dateText;
                if (!article.m_author.isEmpty())
                {
                    subtitle = subtitle.isEmpty() ? article.m_author : subtitle + tr(" - ") + article.m_author;
                }
                m_newsGrid->addWidget(CreateMediaCard(chip, article.m_title, subtitle, c1, c2, article.m_url), i / 3, i % 3);
                ++i;
            }
        }
        else
        {
            // Evergreen backup cards so the News tab is never empty when the feed is unreachable.
            m_newsGrid->addWidget(CreateMediaCard(tr("Docs"), tr("O3DE Documentation"), tr("Guides, tutorials, API reference"), QColor(0x1E, 0x70, 0xEB), QColor(0x0B, 0x2A, 0x52), Url::Docs), 0, 0);
            m_newsGrid->addWidget(CreateMediaCard(tr("Community"), tr("Join the Discord"), tr("Live help and chat"), QColor(0x58, 0x65, 0xF2), QColor(0x37, 0x30, 0xA3), Url::Discord), 0, 1);
            m_newsGrid->addWidget(CreateMediaCard(tr("Source"), tr("O3DE on GitHub"), tr("Engine, gems and samples"), QColor(0x24, 0x29, 0x2F), QColor(0x0E, 0xA5, 0xA4), Url::GitHubOrg), 0, 2);
        }
    }
}

void WelcomeScreenDialog::AddToolRow(const QString& name, const QIcon& icon, const QString& clickUrl)
{
    // One row = [icon] [tool name]. Click is dispatched by scheme in eventFilter (tool: / pane:).
    QWidget* row = new QWidget();
    row->setObjectName(QStringLiteral("welcomeNewsRow"));
    row->setAttribute(Qt::WA_StyledBackground, true);
    row->setAttribute(Qt::WA_Hover, true);
    row->setProperty("welcomeCardUrl", clickUrl);
    row->setCursor(Qt::PointingHandCursor);
    row->installEventFilter(this);

    QHBoxLayout* rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(14, 15, 14, 15);   // generous vertical breathing room per tool
    rowLayout->setSpacing(14);

    QLabel* iconLabel = new QLabel(row);
    iconLabel->setFixedSize(22, 22);
    iconLabel->setPixmap(icon.pixmap(22, 22));
    rowLayout->addWidget(iconLabel, 0, Qt::AlignVCenter);

    QLabel* nameLabel = new QLabel(name, row);
    nameLabel->setObjectName(QStringLiteral("welcomeNewsRowTitle"));
    rowLayout->addWidget(nameLabel, 1);

    m_railFeedLayout->addWidget(row);
}

void WelcomeScreenDialog::PopulateProjectTools()
{
    if (!m_railFeedLayout)
    {
        return;
    }

    QFileIconProvider iconProvider;

    // 1) Standalone authoring apps that ship next to the Editor (Project Manager intentionally excluded).
    //    Asset Processor is single-instance, so launching it focuses the one already serving the project.
    struct ExeTool { const char* m_exe; const char* m_label; };
    static const ExeTool exeTools[] = {
        { "MaterialEditor",          "Material Editor" },
        { "MaterialCanvas",          "Material Canvas" },
        { "PassCanvas",              "Pass Canvas" },
        { "ShaderManagementConsole", "Shader Management Console" },
        { "AssetProcessor",          "Asset Processor" },
    };

    const auto binDir = AZ::Utils::GetExecutableDirectory();
    const QString binDirPath = QString::fromUtf8(binDir.c_str());
#if defined(AZ_PLATFORM_WINDOWS)
    const QString suffix = QStringLiteral(".exe");
#else
    const QString suffix;
#endif

    for (const ExeTool& tool : exeTools)
    {
        const QString exePath = binDirPath + QLatin1Char('/') + QString::fromLatin1(tool.m_exe) + suffix;
        const QFileInfo exeInfo(exePath);
        if (exeInfo.exists())
        {
            AddToolRow(QString::fromLatin1(tool.m_label), iconProvider.icon(exeInfo), QStringLiteral("tool:") + exePath);
        }
    }

    // 2) In-editor authoring tools from the Tools menu (opened as view panes). Shown only when actually
    //    registered, so e.g. Landscape Canvas appears only when its gem is enabled.
    struct PaneTool { const char* m_pane; const char* m_label; };
    static const PaneTool paneTools[] = {
        { LyViewPane::ScriptCanvas,    "Script Canvas" },
        { LyViewPane::LandscapeCanvas, "Landscape Canvas" },
        { LyViewPane::UiEditor,        "UI Editor" },
        { LyViewPane::TrackView,       "Track View" },
    };

    const QIcon paneIcon = style()->standardIcon(QStyle::SP_FileDialogContentsView);
    if (QtViewPaneManager::exists())
    {
        for (const PaneTool& tool : paneTools)
        {
            if (QtViewPaneManager::instance()->GetPane(QString::fromLatin1(tool.m_pane)))
            {
                AddToolRow(QString::fromLatin1(tool.m_label), paneIcon, QStringLiteral("pane:") + QString::fromLatin1(tool.m_pane));
            }
        }
    }

    if (m_railFeedLayout->count() == 0)
    {
        QLabel* none = new QLabel(tr("No authoring tools were found for this project."));
        none->setObjectName(QStringLiteral("welcomeNewsRowDate"));
        none->setWordWrap(true);
        m_railFeedLayout->addWidget(none);
    }
}

QWidget* WelcomeScreenDialog::CreateCommunityPage()
{
    const int pageMargin = ThemeMetric("WelcomeScreenPageMargin", 24);
    const int sectionSpacing = ThemeMetric("WelcomeScreenSectionSpacing", 12);
    QVBoxLayout* layout = nullptr;
    QWidget* page = MakeScrollPage(layout, pageMargin, sectionSpacing);

    layout->addWidget(CreateSectionLabel(tr("IF YOU NEED HELP")));
    QGridLayout* help = new QGridLayout();
    help->setSpacing(16);
    help->addWidget(CreateInfoCard(QColor(0x58, 0x65, 0xF2), QStringLiteral("D"), tr("Discord - live support"),
        tr("Real-time help and chat with the O3DE community. The fastest way to get unstuck."),
        tr("Join the Discord  >"), Url::Discord), 0, 0);
    help->addWidget(CreateInfoCard(QColor(0xFF, 0x45, 0x00), QStringLiteral("R"), tr("Reddit - support forum"),
        tr("Ask questions, search past answers, and follow longer discussions at r/O3DE."),
        tr("Open the forum  >"), Url::Reddit), 0, 1);
    layout->addLayout(help);

    layout->addWidget(CreateSectionLabel(tr("FOLLOW AND CONNECT")));
    QGridLayout* social = new QGridLayout();
    social->setSpacing(14);
    social->addWidget(CreateSocialTile(tr("YouTube"), QStringLiteral("@O3DEngine"), QColor(0xFF, 0x00, 0x00), Url::YouTube), 0, 0);
    social->addWidget(CreateSocialTile(tr("Twitch"), QStringLiteral("twitch.tv/o3de"), QColor(0x91, 0x46, 0xFF), Url::Twitch), 0, 1);
    social->addWidget(CreateSocialTile(tr("X"), QStringLiteral("@o3dengine"), QColor(0x11, 0x11, 0x11), Url::Twitter), 0, 2);
    social->addWidget(CreateSocialTile(tr("LinkedIn"), QStringLiteral("Open 3D Engine"), QColor(0x0A, 0x66, 0xC2), Url::LinkedIn), 1, 0);
    social->addWidget(CreateSocialTile(tr("Mastodon"), QStringLiteral("@O3DF"), QColor(0x63, 0x64, 0xFF), Url::Mastodon), 1, 1);
    social->addWidget(CreateSocialTile(tr("GitHub Discussions"), QStringLiteral("Questions and answers"), QColor(0x24, 0x29, 0x2F), Url::Discussions), 1, 2);
    layout->addLayout(social);
    layout->addStretch();
    return page;
}

QWidget* WelcomeScreenDialog::CreateResourcesPage()
{
    const int pageMargin = ThemeMetric("WelcomeScreenPageMargin", 24);
    const int sectionSpacing = ThemeMetric("WelcomeScreenSectionSpacing", 12);
    QVBoxLayout* layout = nullptr;
    QWidget* page = MakeScrollPage(layout, pageMargin, sectionSpacing);

    layout->addWidget(CreateSectionLabel(tr("DOCUMENTATION")));
    QGridLayout* docs = new QGridLayout();
    docs->setSpacing(16);
    docs->addWidget(CreateMediaCard(QString(), tr("O3DE Documentation"), tr("Guides, tutorials, API reference"), QColor(0x1E, 0x70, 0xEB), QColor(0x0B, 0x2A, 0x52), Url::Docs), 0, 0);
    docs->addWidget(CreateMediaCard(QString(), tr("Getting started"), tr("From install to your first level"), QColor(0x26, 0x80, 0xEB), QColor(0x6A, 0x41, 0xA4), Url::Docs), 0, 1);
    layout->addLayout(docs);

    layout->addWidget(CreateSectionLabel(tr("SOURCE AND REPOSITORIES")));
    QGridLayout* repos = new QGridLayout();
    repos->setSpacing(14);
    repos->addWidget(CreateInfoCard(QColor(0x24, 0x29, 0x2F), QStringLiteral("GH"), tr("O3DE on GitHub"), tr("The o3de organization"), tr("Open  >"), Url::GitHubOrg), 0, 0);
    repos->addWidget(CreateInfoCard(QColor(0x26, 0x80, 0xEB), QStringLiteral("EN"), tr("Engine source"), tr("o3de/o3de"), tr("Open  >"), Url::GitHub), 0, 1);
    repos->addWidget(CreateInfoCard(QColor(0x43, 0xD9, 0x6A), QStringLiteral("EX"), tr("Gems and samples"), tr("o3de-extras"), tr("Open  >"), Url::RepoExtras), 0, 2);
    layout->addLayout(repos);

    layout->addWidget(CreateSectionLabel(tr("SAMPLE AND REFERENCE PROJECTS")));
    QGridLayout* samples = new QGridLayout();
    samples->setSpacing(16);
    samples->addWidget(CreateMediaCard(tr("Sample"), tr("Planet Survival Game"), tr("o3de/PlanetSurvivalGame"), QColor(0x8B, 0x5C, 0xF6), QColor(0xE2, 0x52, 0x43), "https://github.com/o3de/PlanetSurvivalGame"), 0, 0);
    samples->addWidget(CreateMediaCard(tr("Sample"), tr("Multiplayer Sample"), tr("o3de/o3de-multiplayersample"), QColor(0x0E, 0xA5, 0xA4), QColor(0x26, 0x80, 0xEB), "https://github.com/o3de/o3de-multiplayersample"), 0, 1);
    samples->addWidget(CreateMediaCard(tr("Rendering"), tr("Atom SampleViewer"), tr("o3de/o3de-atom-sampleviewer"), QColor(0x22, 0xD3, 0xEE), QColor(0x37, 0x30, 0xA3), "https://github.com/o3de/o3de-atom-sampleviewer"), 0, 2);
    layout->addLayout(samples);
    layout->addStretch();
    return page;
}

QWidget* WelcomeScreenDialog::CreateContributePage()
{
    const int pageMargin = ThemeMetric("WelcomeScreenPageMargin", 24);
    const int sectionSpacing = ThemeMetric("WelcomeScreenSectionSpacing", 12);
    QVBoxLayout* layout = nullptr;
    QWidget* page = MakeScrollPage(layout, pageMargin, sectionSpacing);

    // Donate panel (leads the page).
    QWidget* donate = new QWidget();
    donate->setObjectName(QStringLiteral("welcomeDonatePanel"));
    donate->setAttribute(Qt::WA_StyledBackground, true);
    QVBoxLayout* donateLayout = new QVBoxLayout(donate);
    donateLayout->setContentsMargins(24, 24, 24, 24);
    donateLayout->setSpacing(10);
    QLabel* dTitle = new QLabel(tr("Support O3DE's development"), donate);
    dTitle->setObjectName(QStringLiteral("welcomeHeroTitle"));
    QLabel* dBody = new QLabel(
        tr("O3DE is free, open source, and community-led. It keeps moving forward thanks to the people "
           "and organizations who support it. If O3DE is useful to you, please consider chipping in."),
        donate);
    dBody->setObjectName(QStringLiteral("welcomeCardBody"));
    dBody->setWordWrap(true);
    QPushButton* donateButton = new QPushButton(tr("Donate to O3DE"), donate);
    donateButton->setObjectName(QStringLiteral("welcomeDonateButton"));
    donateButton->setFocusPolicy(Qt::NoFocus);
    donateButton->setMinimumHeight(40);
    connect(donateButton, &QPushButton::clicked, this, []{ OpenExternalUrl(Url::Donate); });
    QHBoxLayout* donateButtonRow = new QHBoxLayout();
    donateButtonRow->addWidget(donateButton);
    donateButtonRow->addStretch();
    donateLayout->addWidget(dTitle);
    donateLayout->addWidget(dBody);
    donateLayout->addLayout(donateButtonRow);
    layout->addWidget(donate);

    layout->addWidget(CreateSectionLabel(tr("WAYS TO CONTRIBUTE")));
    QGridLayout* ways = new QGridLayout();
    ways->setSpacing(16);
    ways->addWidget(CreateInfoCard(QColor(0x26, 0x80, 0xEB), QStringLiteral("</>"), tr("Write code"), tr("Fix bugs and build new features across the engine and its gems."), QString(), Url::GitHub), 0, 0);
    ways->addWidget(CreateInfoCard(QColor(0x0E, 0xA5, 0xA4), QStringLiteral("Doc"), tr("Improve docs"), tr("Write tutorials, fix typos, and clarify the guides."), QString(), Url::RepoDocs), 0, 1);
    ways->addWidget(CreateInfoCard(QColor(0x8B, 0x5C, 0xF6), QStringLiteral("Adv"), tr("Advocate and create"), tr("Stream, write, make videos, and share what you build with O3DE."), QString(), Url::Contribute), 0, 2);
    ways->addWidget(CreateInfoCard(QColor(0x43, 0xD9, 0x6A), QStringLiteral("Com"), tr("Help others"), tr("Answer questions and welcome newcomers in the community."), QString(), Url::Discord), 1, 0);
    ways->addWidget(CreateInfoCard(QColor(0xE2, 0x52, 0x43), QStringLiteral("Sec"), tr("Report security"), tr("Responsibly disclose and help fix vulnerabilities."), QString(), Url::Contribute), 1, 1);
    ways->addWidget(CreateInfoCard(QColor(0xF0, 0xC3, 0x2D), QStringLiteral("SIG"), tr("Join a SIG"), tr("Help steer part of the engine through a Special Interest Group."), QString(), Url::Contribute), 1, 2);
    layout->addLayout(ways);
    layout->addStretch();
    return page;
}

QWidget* WelcomeScreenDialog::CreateSupportFooter()
{
    const int pageMargin = ThemeMetric("WelcomeScreenPageMargin", 24);
    const int footerPadding = ThemeMetric("WelcomeScreenFooterPadding", 8);

    QWidget* footer = new QWidget(this);
    footer->setObjectName(QStringLiteral("welcomeSupportFooter"));

    QHBoxLayout* layout = new QHBoxLayout(footer);
    layout->setContentsMargins(pageMargin, footerPadding, pageMargin, footerPadding);

    // Rich text so the heart renders from an HTML entity (keeps the source ASCII -- no unicode).
    QLabel* message = new QLabel(footer);
    message->setObjectName(QStringLiteral("welcomeFooterMessage"));
    message->setTextFormat(Qt::RichText);
    message->setText(tr("O3DE is free and community-led. Built with "
                        "<span style=\"color:#E25243;\">&#9829;</span> by the O3DE community."));

    QPushButton* supportButton = new QPushButton(tr("Support the project"), footer);
    supportButton->setObjectName(QStringLiteral("welcomeSupportButton"));
    supportButton->setFocusPolicy(Qt::NoFocus);
    // Gold coin glyph drawn in code (no unicode / no extra resource).
    QPixmap coin(16, 16);
    coin.fill(Qt::transparent);
    {
        QPainter painter(&coin);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setBrush(QColor(0xF0, 0xC3, 0x2D));
        painter.setPen(QPen(QColor(0xA9, 0x81, 0x0C), 1.4));
        painter.drawEllipse(QRectF(1.2, 1.2, 13.6, 13.6));
    }
    supportButton->setIcon(QIcon(coin));
    connect(supportButton, &QPushButton::clicked, this, []{ OpenExternalUrl(Url::Donate); });

    layout->addWidget(message);
    layout->addStretch();
    layout->addWidget(supportButton);
    return footer;
}

void WelcomeScreenDialog::done(int result)
{
    QDialog::done(result);
}

const QString& WelcomeScreenDialog::GetLevelPath()
{
    return m_levelPath;
}

bool WelcomeScreenDialog::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::MouseButtonRelease)
    {
        // A bespoke tab was clicked -> switch the stacked body + active state.
        const QVariant tabIndex = watched->property("tabIndex");
        if (tabIndex.isValid())
        {
            SetActiveTab(tabIndex.toInt());
            return true;
        }
        // A bespoke level tile was clicked -> open that level.
        const QVariant levelIndex = watched->property("welcomeLevelIndex");
        if (levelIndex.isValid())
        {
            const int index = levelIndex.toInt();
            if (index >= 0 && index < static_cast<int>(m_levels.size()))
            {
                m_levelPath = m_levels[index].second;
                accept();
            }
            return true;
        }
        // A content card was clicked. "action:" cards run an internal command (used by the offline
        // fallback); everything else opens its URL externally.
        const QVariant cardUrl = watched->property("welcomeCardUrl");
        if (cardUrl.isValid())
        {
            const QString url = cardUrl.toString();
            if (url.startsWith(QLatin1String("tool:")))
            {
                // Launch the standalone tool, then open a level so the editor comes up with context.
                QProcess::startDetached(url.mid(5), QStringList());
                if (!m_levels.empty())
                {
                    m_levelPath = m_levels.front().second;
                }
                accept();
            }
            else if (url.startsWith(QLatin1String("pane:")))
            {
                // In-editor tool: open its view pane, then open a level so it has context.
                AzToolsFramework::OpenViewPane(url.mid(5).toUtf8().constData());
                if (!m_levels.empty())
                {
                    m_levelPath = m_levels.front().second;
                }
                accept();
            }
            else if (url.startsWith(QLatin1String("action:")))
            {
                const QString action = url.mid(7);
                if (action == QLatin1String("new"))
                {
                    OnNewLevelBtnClicked(true);
                }
                else if (action == QLatin1String("open"))
                {
                    OnOpenLevelBtnClicked(true);
                }
                else if (action == QLatin1String("resume"))
                {
                    OnResumeClicked();
                }
                else if (action == QLatin1String("community"))
                {
                    SetActiveTab(m_communityTabIndex);
                }
            }
            else if (!url.isEmpty())
            {
                QDesktopServices::openUrl(QUrl(url));
            }
            return true;
        }
    }
    return QDialog::eventFilter(watched, event);
}

bool WelcomeScreenDialog::IsValidLevelName(const QString& path)
{
    QStringList pathParts = Path::SplitIntoSegments(path);

    QString levelName = pathParts.at(pathParts.size() - 1);

    if (levelName.endsWith(".prefab", Qt::CaseInsensitive))
    {
        // If the level is a prefab, check the container name.
        levelName = pathParts.at(pathParts.size() - 2);
    }

    QRegularExpressionValidator validator(QRegularExpression("^[a-zA-Z0-9_\\-./]*$"));

    int pos = 0;
    return validator.validate(levelName, pos);
}

void WelcomeScreenDialog::SetRecentFileList(RecentFileList* pList)
{
    if (!pList)
    {
        return;
    }

    m_pRecentList = pList;

    // Rebuild the bespoke level tiles from scratch.
    m_levels.clear();
    if (m_recentLevelsLayout)
    {
        while (QLayoutItem* item = m_recentLevelsLayout->takeAt(0))
        {
            delete item->widget();
            delete item;
        }
    }

    auto projectPath = AZ::Utils::GetProjectPath();
    QString gamePath{projectPath.c_str()};
    Path::ConvertSlashToBackSlash(gamePath);
    gamePath = Path::ToUnixPath(gamePath.toLower());
    gamePath = Path::AddSlash(gamePath);

    QString sCurDir = (Path::GetEditingGameDataFolder() + QDir::separator().toLatin1()).c_str();
    int nCurDir = static_cast<int>(sCurDir.length());

    int recentListSize = pList->GetSize();
    for (int i = 0; i < recentListSize; ++i)
    {
        const QString& recentFile = pList->m_arrNames[i];
        if (recentFile.endsWith(m_levelExtension) && IsValidLevelName(recentFile))
        {
            if (CFileUtil::Exists(recentFile, false))
            {
                // Accept project-rooted entries (legacy fast path) or any
                // entry that lives inside an active gem's source tree, so
                // gem-rooted levels show up alongside project ones.
                const QString sCurEntryDir = recentFile.left(nCurDir);
                const bool isProjectRooted = sCurEntryDir.compare(sCurDir, Qt::CaseInsensitive) == 0;
                const bool isGemRooted = !isProjectRooted && LevelRoots::IsPathUnderActiveSource(recentFile);
                if (isProjectRooted || isGemRooted)
                {
                    QString fullPath = recentFile;
                    const QString name = Path::GetFile(fullPath);

                    Path::ConvertSlashToBackSlash(fullPath);
                    fullPath = Path::ToUnixPath(fullPath.toLower());
                    fullPath = Path::AddSlash(fullPath);

                    // For project-rooted levels keep the original belt-and-braces
                    // gamePath substring check; gem-rooted entries already passed
                    // the active-source check above and skip it.
                    if (isGemRooted || fullPath.contains(gamePath))
                    {
                        QFileInfo file(recentFile);
                        QDateTime dateTime = file.lastModified();
                        QString date = QLocale::system().toString(dateTime.date(), QLocale::ShortFormat) + " " +
                            QLocale::system().toString(dateTime.time(), QLocale::ShortFormat);

                        const int index = static_cast<int>(m_levels.size());
                        m_levels.push_back(std::make_pair(name, recentFile));
                        if (m_recentLevelsLayout)
                        {
                            m_recentLevelsLayout->addWidget(BuildLevelTile(name, date, index));
                        }
                    }
                }
            }
        }
    }

    // Drive the hero state now that the recent levels are known: resume the most-recent level if any,
    // otherwise the first-run (create / open) state.
    UpdateHeroState(m_levels.empty() ? QString() : m_levels.front().first);
}


void WelcomeScreenDialog::RemoveLevelEntry(int index)
{
    TNamePathPair levelPath = m_levels[index];

    ui->recentLevelTable->removeRow(index);
    m_levels.erase(m_levels.begin() + index);


    if (!m_pRecentList)
    {
        return;
    }

    for (int i = 0; i < m_pRecentList->GetSize(); ++i)
    {
        QString fullPath = m_pRecentList->m_arrNames[i];
        QString fullPath2 = levelPath.second;

        // path from recent list
        Path::ConvertSlashToBackSlash(fullPath);
        fullPath = Path::ToUnixPath(fullPath.toLower());
        fullPath = Path::AddPathSlash(fullPath);

        // path from our dashboard list
        Path::ConvertSlashToBackSlash(fullPath2);
        fullPath2 = Path::ToUnixPath(fullPath2.toLower());
        fullPath2 = Path::AddPathSlash(fullPath2);

        if (fullPath == fullPath2)
        {
            m_pRecentList->Remove(index);
            break;
        }
    }

    m_pRecentList->WriteList();
}


void WelcomeScreenDialog::OnShowToolTip(const QModelIndex& index)
{
    const QString& fullPath = m_levels[index.row()].second;

    QToolTip::showText(QCursor::pos(), QString("Open level: %1").arg(fullPath));
}


void WelcomeScreenDialog::OnShowContextMenu(const QPoint& pos)
{
    QModelIndex index = ui->recentLevelTable->indexAt(pos);
    if (index.isValid())
    {
        QString level = ui->recentLevelTable->itemAt(pos)->text();

        QPoint globalPos = ui->recentLevelTable->viewport()->mapToGlobal(pos);

        QMenu contextMenu;
        contextMenu.addAction(QString("Remove " + level + " from recent list"));
        QAction* selectedItem = contextMenu.exec(globalPos);
        if (selectedItem)
        {
            RemoveLevelEntry(index.row());
        }
    }
}

void WelcomeScreenDialog::OnNewLevelBtnClicked([[maybe_unused]] bool checked)
{
    m_levelPath = "new";
    accept();
}

void WelcomeScreenDialog::OnNewLevelLabelClicked(const QString& path)
{
    if (path == "Create")
    {
        OnNewLevelBtnClicked(true);
    }
    else
    {
        OnOpenLevelBtnClicked(true);
    }
}

void WelcomeScreenDialog::OnOpenLevelBtnClicked([[maybe_unused]] bool checked)
{
    CLevelFileDialog dlg(true, this);

    if (dlg.exec() == QDialog::Accepted)
    {
        m_levelPath = dlg.GetFileName();
        accept();
    }
}

void WelcomeScreenDialog::OnRecentLevelTableItemClicked(const QModelIndex& modelIndex)
{
    int index = modelIndex.row();

    if (index >= 0 && index < m_levels.size())
    {
        m_levelPath = m_levels[index].second;
        accept();
    }
}

void WelcomeScreenDialog::OnCloseBtnClicked([[maybe_unused]] bool checked)
{
    accept();
}

void WelcomeScreenDialog::previewAreaScrolled()
{
    //this should only be reported once per session
    if (m_messageScrollReported)
    {
        return;
    }
    m_messageScrollReported = true;
}

