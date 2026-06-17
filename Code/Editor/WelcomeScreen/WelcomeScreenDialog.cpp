/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include "EditorDefs.h"

#include "WelcomeScreenDialog.h"

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
#include <QTabWidget>
#include <QTabBar>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QUrl>

#include <AzCore/Utils/Utils.h>
#include <AzCore/Interface/Interface.h>


// AzToolsFramework
#include <AzToolsFramework/UI/UICore/WidgetHelpers.h>

// AzQtComponents
#include <AzQtComponents/Components/Widgets/CheckBox.h>
#include <AzQtComponents/Components/WindowDecorationWrapper.h>
#include <AzQtComponents/Components/StyleManagerInterface.h>
#include <AzQtComponents/Utilities/PixmapScaleUtilities.h>

// Editor
#include "Settings.h"
#include "MainWindow.h"
#include "CryEdit.h"
#include "LevelFileDialog.h"
#include "LevelRoots.h"

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

    void OpenExternalUrl(const char* url)
    {
        QDesktopServices::openUrl(QUrl(QString::fromLatin1(url)));
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

void WelcomeScreenDialog::BuildPortalShell()
{
    // The Project page content inset (was the .ui literal) now comes from the theme.
    const int pageMargin = ThemeMetric("WelcomeScreenPageMargin", 24);
    ui->bodyContainer->setContentsMargins(pageMargin, pageMargin, pageMargin, pageMargin);

    // Fixed, non-movable navigation tabs. The Project page is the existing level launcher (already
    // built by the .ui); adding it to the tab widget reparents it out of the dialog's root layout.
    QTabWidget* navTabs = new QTabWidget(this);
    navTabs->setObjectName(QStringLiteral("welcomeNavTabs"));
    navTabs->setDocumentMode(true);
    navTabs->setFocusPolicy(Qt::NoFocus);
    navTabs->tabBar()->setExpanding(false);

    navTabs->addTab(ui->projectPageContainer, tr("Project"));
    navTabs->addTab(CreateNewsPage(), tr("News"));
    navTabs->addTab(CreateCommunityPage(), tr("Community"));
    navTabs->addTab(CreateDocsPage(), tr("Docs"));
    navTabs->addTab(CreateSupportPage(), tr("Support"));

    // Persistent, understated support footer shown beneath every tab.
    QWidget* footer = CreateSupportFooter();

    ui->verticalLayout->setSpacing(0);
    ui->verticalLayout->setContentsMargins(0, 0, 0, 0);
    ui->verticalLayout->addWidget(navTabs, 1);
    ui->verticalLayout->addWidget(footer, 0);
}

void WelcomeScreenDialog::AddLinkButton(QVBoxLayout* layout, const QString& label, const QString& url)
{
    QPushButton* button = new QPushButton(label, layout->parentWidget());
    button->setFocusPolicy(Qt::NoFocus);
    connect(button, &QPushButton::clicked, this, [url]{ QDesktopServices::openUrl(QUrl(url)); });
    layout->addWidget(button);
}

QWidget* WelcomeScreenDialog::CreateNewsPage()
{
    // NOTE: a later stage replaces this with a live, cached feed scraped from o3de.org/news-blogs.
    // Until then the tab links out to the real content so it is useful from day one.
    const int pageMargin = ThemeMetric("WelcomeScreenPageMargin", 24);
    const int contentSpacing = ThemeMetric("WelcomeScreenContentSpacing", 8);

    QWidget* page = new QWidget(this);
    QVBoxLayout* layout = new QVBoxLayout(page);
    layout->setContentsMargins(pageMargin, pageMargin, pageMargin, pageMargin);
    layout->setSpacing(contentSpacing);

    QLabel* title = new QLabel(tr("News and Blogs"), page);
    title->setProperty("fontStyle", "sectionTitle");
    layout->addWidget(title);

    QLabel* intro = new QLabel(tr("Releases, community spotlights, and what is happening across the O3DE project."), page);
    intro->setWordWrap(true);
    layout->addWidget(intro);
    layout->addSpacing(contentSpacing);

    AddLinkButton(layout, tr("Browse all O3DE news and blogs"), QString::fromLatin1(Url::NewsBlogs));
    AddLinkButton(layout, tr("O3DE on YouTube"), QString::fromLatin1(Url::YouTube));
    AddLinkButton(layout, tr("O3DE podcast on Spotify"), QString::fromLatin1(Url::Spotify));

    layout->addStretch();
    return page;
}

QWidget* WelcomeScreenDialog::CreateCommunityPage()
{
    const int pageMargin = ThemeMetric("WelcomeScreenPageMargin", 24);
    const int contentSpacing = ThemeMetric("WelcomeScreenContentSpacing", 8);
    const int sectionSpacing = ThemeMetric("WelcomeScreenSectionSpacing", 12);

    QWidget* page = new QWidget(this);
    QVBoxLayout* layout = new QVBoxLayout(page);
    layout->setContentsMargins(pageMargin, pageMargin, pageMargin, pageMargin);
    layout->setSpacing(contentSpacing);

    QLabel* title = new QLabel(tr("Community"), page);
    title->setProperty("fontStyle", "sectionTitle");
    layout->addWidget(title);

    QLabel* intro = new QLabel(
        tr("Discord is the central place to get help, ask questions, and connect with other O3DE developers."),
        page);
    intro->setWordWrap(true);
    layout->addWidget(intro);
    layout->addSpacing(contentSpacing);

    // Discord is the primary support and chat hub, so give it a prominent, dedicated button.
    QPushButton* discordButton = new QPushButton(tr("Join the O3DE Discord"), page);
    discordButton->setObjectName(QStringLiteral("welcomeDiscordButton"));
    discordButton->setFocusPolicy(Qt::NoFocus);
    connect(discordButton, &QPushButton::clicked, this, []{ OpenExternalUrl(Url::Discord); });
    layout->addWidget(discordButton);

    AddLinkButton(layout, tr("GitHub Discussions"), QString::fromLatin1(Url::Discussions));
    AddLinkButton(layout, tr("Reddit (r/O3DE)"), QString::fromLatin1(Url::Reddit));

    layout->addSpacing(sectionSpacing);
    QLabel* followLabel = new QLabel(tr("Follow O3DE"), page);
    followLabel->setProperty("fontStyle", "sectionTitle");
    layout->addWidget(followLabel);

    AddLinkButton(layout, tr("YouTube"), QString::fromLatin1(Url::YouTube));
    AddLinkButton(layout, tr("Twitch"), QString::fromLatin1(Url::Twitch));
    AddLinkButton(layout, tr("X (Twitter)"), QString::fromLatin1(Url::Twitter));
    AddLinkButton(layout, tr("LinkedIn"), QString::fromLatin1(Url::LinkedIn));
    AddLinkButton(layout, tr("Mastodon"), QString::fromLatin1(Url::Mastodon));

    layout->addStretch();
    return page;
}

QWidget* WelcomeScreenDialog::CreateDocsPage()
{
    const int pageMargin = ThemeMetric("WelcomeScreenPageMargin", 24);
    const int contentSpacing = ThemeMetric("WelcomeScreenContentSpacing", 8);
    const int sectionSpacing = ThemeMetric("WelcomeScreenSectionSpacing", 12);

    QWidget* page = new QWidget(this);
    QVBoxLayout* layout = new QVBoxLayout(page);
    layout->setContentsMargins(pageMargin, pageMargin, pageMargin, pageMargin);
    layout->setSpacing(contentSpacing);

    QLabel* title = new QLabel(tr("Documentation"), page);
    title->setProperty("fontStyle", "sectionTitle");
    layout->addWidget(title);

    QLabel* intro = new QLabel(tr("Guides, tutorials, and API reference for building with O3DE."), page);
    intro->setWordWrap(true);
    layout->addWidget(intro);
    layout->addSpacing(contentSpacing);

    AddLinkButton(layout, tr("O3DE Documentation"), QString::fromLatin1(Url::Docs));

    layout->addSpacing(sectionSpacing);
    QLabel* reposLabel = new QLabel(tr("Source and repositories"), page);
    reposLabel->setProperty("fontStyle", "sectionTitle");
    layout->addWidget(reposLabel);

    AddLinkButton(layout, tr("O3DE on GitHub"), QString::fromLatin1(Url::GitHubOrg));
    AddLinkButton(layout, tr("Engine source (o3de/o3de)"), QString::fromLatin1(Url::GitHub));
    AddLinkButton(layout, tr("Gems and samples (o3de-extras)"), QString::fromLatin1(Url::RepoExtras));
    AddLinkButton(layout, tr("Third-party packages (3p-package-source)"), QString::fromLatin1(Url::RepoThreeP));
    AddLinkButton(layout, tr("Documentation source (o3de.org)"), QString::fromLatin1(Url::RepoDocs));

    layout->addStretch();
    return page;
}

QWidget* WelcomeScreenDialog::CreateSupportPage()
{
    const int pageMargin = ThemeMetric("WelcomeScreenPageMargin", 24);
    const int sectionSpacing = ThemeMetric("WelcomeScreenSectionSpacing", 12);

    QWidget* page = new QWidget(this);
    QVBoxLayout* layout = new QVBoxLayout(page);
    layout->setContentsMargins(pageMargin, pageMargin, pageMargin, pageMargin);
    layout->setSpacing(sectionSpacing);
    layout->addStretch();

    QLabel* title = new QLabel(tr("Support O3DE's development"), page);
    title->setAlignment(Qt::AlignHCenter);
    title->setProperty("fontStyle", "sectionTitle");

    QLabel* blurb = new QLabel(
        tr("O3DE is free, open source, and community-led. It keeps moving forward thanks to the people "
           "and organizations who support it. If O3DE is useful to you, please consider chipping in."),
        page);
    blurb->setAlignment(Qt::AlignHCenter);
    blurb->setWordWrap(true);
    blurb->setMaximumWidth(540);

    QHBoxLayout* buttonRow = new QHBoxLayout();
    buttonRow->addStretch();
    QPushButton* donateButton = new QPushButton(tr("Donate"), page);
    connect(donateButton, &QPushButton::clicked, this, []{ OpenExternalUrl(Url::Donate); });
    QPushButton* contributeButton = new QPushButton(tr("Other ways to contribute"), page);
    connect(contributeButton, &QPushButton::clicked, this, []{ OpenExternalUrl(Url::Contribute); });
    buttonRow->addWidget(donateButton);
    buttonRow->addWidget(contributeButton);
    buttonRow->addStretch();

    layout->addWidget(title);
    layout->addWidget(blurb, 0, Qt::AlignHCenter);
    layout->addLayout(buttonRow);
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

    QLabel* message = new QLabel(tr("O3DE is free and community-led."), footer);

    QPushButton* supportButton = new QPushButton(tr("Support O3DE"), footer);
    supportButton->setObjectName(QStringLiteral("welcomeSupportButton"));
    supportButton->setFocusPolicy(Qt::NoFocus);
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
    if (event->type() == QEvent::Show)
    {
        ui->recentLevelTable->horizontalHeader()->resizeSection(0, ui->nameLabel->width());
        ui->recentLevelTable->horizontalHeader()->resizeSection(1, ui->modifiedLabel->width());
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

    auto projectPath = AZ::Utils::GetProjectPath();
    QString gamePath{projectPath.c_str()};
    Path::ConvertSlashToBackSlash(gamePath);
    gamePath = Path::ToUnixPath(gamePath.toLower());
    gamePath = Path::AddSlash(gamePath);

    QString sCurDir = (Path::GetEditingGameDataFolder() + QDir::separator().toLatin1()).c_str();
    int nCurDir = static_cast<int>(sCurDir.length());

    int recentListSize = pList->GetSize();
    int currentRow = 0;
    ui->recentLevelTable->setRowCount(recentListSize);
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
                        if (gSettings.prefabSystem)
                        {
                            QIcon icon;
                            icon.addFile(QString::fromUtf8(":/Level/level.svg"), QSize(), QIcon::Normal, QIcon::Off);
                            ui->recentLevelTable->setItem(currentRow, 0, new QTableWidgetItem(icon, name));
                        }
                        else
                        {
                            ui->recentLevelTable->setItem(currentRow, 0, new QTableWidgetItem(name));
                        }
                        QFileInfo file(recentFile);
                        QDateTime dateTime = file.lastModified();
                        QString date = QLocale::system().toString(dateTime.date(), QLocale::ShortFormat) + " " +
                            QLocale::system().toString(dateTime.time(), QLocale::LongFormat);
                        ui->recentLevelTable->setItem(currentRow++, 1, new QTableWidgetItem(date));
                        m_levels.push_back(std::make_pair(name, recentFile));
                    }
                }
            }
        }
    }
    ui->recentLevelTable->setRowCount(currentRow);
    ui->recentLevelTable->setMinimumHeight(currentRow * ui->recentLevelTable->verticalHeader()->defaultSectionSize());
    ui->recentLevelTable->setMaximumHeight(currentRow * ui->recentLevelTable->verticalHeader()->defaultSectionSize());
    ui->levelFileLabel->setVisible(currentRow ? false : true);

    ui->recentLevelTable->setCurrentIndex(QModelIndex());
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

