/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */
#pragma once

#include <QDialog>
#include <QStyle>   // QStyle::StandardPixmap used in a method signature below

namespace News {
    class ResourceManifest;
    class ArticleViewContainer;
}

namespace Ui {
    class WelcomeScreenDialog;
}

class QStringListModel;
class QVBoxLayout;
class QGridLayout;
class QLabel;
class QStackedWidget;
class QPushButton;
class QColor;
class QPixmap;
class QIcon;
class RecentFileList;

namespace O3DEWelcome {
    class WelcomeNewsFeed;
}

class WelcomeScreenDialog
    : public QDialog
{
    Q_OBJECT
public:
    WelcomeScreenDialog(QWidget* pParent = nullptr);
    ~WelcomeScreenDialog();

    const QString& GetLevelPath();
    void SetRecentFileList(RecentFileList* pList);

    bool eventFilter(QObject *watched, QEvent *event) override;

public Q_SLOTS:
    void done(int result) override;

private:
    typedef std::pair<QString, QString> TNamePathPair;
    typedef std::vector<TNamePathPair> TNameFullPathArray;

    Ui::WelcomeScreenDialog* ui;

    QString m_levelPath;
    TNameFullPathArray m_levels;
    RecentFileList* m_pRecentList;
    const char* m_levelExtension = nullptr;
    bool m_messageScrollReported = false;

    // Welcome portal hero (dynamic resume / first-run state) + tab nav.
    QStackedWidget* m_navStack = nullptr;
    QList<QLabel*> m_tabLabels;      // bespoke tab labels (NOT QPushButtons -- O3DE custom-paints those)
    int m_communityTabIndex = -1;
    QLabel* m_heroEyebrow = nullptr;
    QLabel* m_heroMeta = nullptr;
    QWidget* m_heroCtaContainer = nullptr;
    QVBoxLayout* m_recentLevelsLayout = nullptr;

    // Live news feed (o3de.org) + the views it drives. RefreshNewsViews() repopulates both the News
    // tab grid and the Project-tab "Latest Updates" rail, including the offline fallback content.
    O3DEWelcome::WelcomeNewsFeed* m_newsFeed = nullptr;
    QGridLayout* m_newsGrid = nullptr;
    QVBoxLayout* m_railFeedLayout = nullptr;
    QLabel* m_newsStatusLabel = nullptr;
    QLabel* m_railLabel = nullptr;   // "LATEST UPDATES" (feed) vs "HELPFUL RESOURCES" (fallback)

    bool IsValidLevelName(const QString& path);
    void RemoveLevelEntry(int index);

    // ---- Welcome portal shell (fixed nav tabs + persistent support footer) ----
    int ThemeMetric(const char* token, int fallback) const;
    void ApplyBrandTitleBar();
    void ApplyWelcomeStyle();
    void BuildPortalShell();
    QWidget* BuildProjectPage();
    QWidget* BuildQuickActions(QWidget* parent);
    QWidget* BuildLevelTile(const QString& name, const QString& dateText, int index);
    void UpdateHeroState(const QString& lastLevelName);
    void OnResumeClicked();
    // Bespoke clickable widgets that bypass O3DE's custom QPushButton painter (which clips/!resizes).
    QLabel* MakeBespokeButton(const QString& text, const QString& objectName, const QString& clickUrl);
    QWidget* MakeQuickAction(const QString& text, QStyle::StandardPixmap icon, const QString& clickUrl);
    void SetActiveTab(int index);
    // ---- Bespoke tab bodies + reusable card components ----
    QWidget* CreateNewsPage();
    QWidget* CreateCommunityPage();
    QWidget* CreateResourcesPage();
    QWidget* CreateContributePage();
    QWidget* CreateSupportFooter();
    QWidget* CreateInfoCard(const QColor& iconColor, const QString& iconGlyph, const QString& title,
                            const QString& body, const QString& linkText, const QString& url);
    QWidget* CreateMediaCard(const QString& chip, const QString& title, const QString& subtitle,
                             const QColor& c1, const QColor& c2, const QString& url);
    QWidget* CreateSocialTile(const QString& name, const QString& handle, const QColor& brand, const QString& url);
    // Quiet, low-key news entry for the Project-page rail (no large gradient thumbnail).
    QWidget* CreateCompactNewsRow(const QString& title, const QString& subtitle, const QColor& accent, const QString& url);
    QWidget* CreateSectionLabel(const QString& text);
    static QPixmap MakeGradientPixmap(const QColor& c1, const QColor& c2, const QString& monogram, const QSize& size);

    // Rebuilds the News grid from the feed's current state. Connected to WelcomeNewsFeed::Updated.
    void RefreshNewsViews();

    // Fills the Project-page rail with launchers: standalone authoring apps next to the Editor, plus
    // in-editor authoring tools (view panes) that are registered. Launching also opens a level.
    void PopulateProjectTools();
    void AddToolRow(const QString& name, const QIcon& icon, const QString& clickUrl);

    void OnShowToolTip(const QModelIndex& index);
    void OnShowContextMenu(const QPoint& point);
    void OnNewLevelBtnClicked(bool checked);
    void OnNewLevelLabelClicked(const QString& checked);
    void OnOpenLevelBtnClicked(bool checked);
    void OnRecentLevelTableItemClicked(const QModelIndex& index);
    void OnCloseBtnClicked(bool checked);

private Q_SLOTS:
    void previewAreaScrolled();
};

