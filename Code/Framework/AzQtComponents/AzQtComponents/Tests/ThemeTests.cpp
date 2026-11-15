/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include <AzCore/UnitTest/TestTypes.h>
#include <AzCore/std/smart_ptr/unique_ptr.h>
#include <AzTest/AzTest.h>

#include <AzQtComponents/Components/Elements/Foundations.h>
#include <AzQtComponents/Components/Theme/ThemeEditor.h>
#include <AzQtComponents/Components/Theme/ThemeManager.h>
#include <AzQtComponents/Components/Theme/ThemeToken.h>
#include <AzQtComponents/Components/Theme/ThemeTokens.h>

#include <QApplication>
#include <QBrush>
#include <QColor>
#include <QAbstractButton>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QLineEdit>
#include <QMargins>
#include <QSet>
#include <QSpinBox>
#include <QTabBar>
#include <QTemporaryDir>

namespace AzQtComponents
{
    //! Everything the theme tests share. A named namespace: the test files of this module are built as one translation unit.
    namespace ThemeTestsInternal
    {
        // ------------------------------------------------------------------------------
        // A catalogue of the tests' own: tokens, a reference, and values built from them
        // ------------------------------------------------------------------------------

        const QColor BaseOriginal(0x10, 0x20, 0x30);
        const QColor OtherColor(0xA0, 0xB0, 0xC0);

        ThemeColor Base{ BaseOriginal, "themetest.Base", ThemeLevel::Basic, "Base" };
        ThemeColor Follower{ Base, "themetest.Follower", ThemeLevel::Advanced, "Follower" };
        ThemeColor SecondFollower{ Follower, "themetest.SecondFollower", ThemeLevel::Advanced, "Second follower" };
        ThemeInt Length{ 4, "themetest.Length", ThemeLevel::Standard, "Length" };
        ThemeMargins Margins{ QMargins(1, 2, 3, 4), "themetest.Margins", ThemeLevel::Advanced, "Margins" };
        ThemeText Family{ "Open Sans", "themetest.Family", ThemeLevel::Advanced, "Family" };
        ThemeInt Described{ 1, "themetest.Described", ThemeLevel::Advanced, "Described", "Where it shows" };

        const QColor& BaseName = Base;
        const int& LengthName = Length;

        Themed<QBrush> BaseBrush = [] { return QBrush(Base); };
        Themed<int> TwiceTheLength = [] { return Length * 2; };
        //! Reads the value above it: values are built again in the order they were first built
        Themed<int> FourTimesTheLength = [] { return TwiceTheLength * 2; };

        QString text(const QColor& color)
        {
            return ThemeTokens::toText(ThemeKind::Color, QVariant::fromValue(color));
        }
    } // namespace ThemeTestsInternal

    using namespace ThemeTestsInternal;

    // ----------------------------------------------------------------------------------
    // Fixture: every test leaves the registry without a theme
    // ----------------------------------------------------------------------------------

    class ThemeTokenTests
        : public ::testing::Test
    {
    public:
        ~ThemeTokenTests() override
        {
            ThemeTokens::setTheme({});
            ThemeTokens::rebuild();
        }
    };

    // ----------------------------------------------------------------------------------
    // T1: tokens and the theme's values
    // ----------------------------------------------------------------------------------

    TEST_F(ThemeTokenTests, Token_HasItsOriginalValue)
    {
        EXPECT_EQ(QColor(Base), BaseOriginal);
        EXPECT_EQ(static_cast<int>(Length), 4);
        EXPECT_EQ(QMargins(Margins), QMargins(1, 2, 3, 4));
        EXPECT_STREQ(Family, "Open Sans");
        EXPECT_FALSE(Base.changed());
    }

    TEST_F(ThemeTokenTests, SetTheme_GivesTheTokensItsValues)
    {
        ThemeTokens::setTheme({ { "themetest.Base", text(OtherColor) }, { "themetest.Length", "9" },
                                { "themetest.Margins", "5 6 7 8" }, { "themetest.Family", "Arial" } });

        EXPECT_EQ(QColor(Base), OtherColor);
        EXPECT_EQ(static_cast<int>(Length), 9);
        EXPECT_EQ(QMargins(Margins), QMargins(5, 6, 7, 8));
        EXPECT_STREQ(Family, "Arial");
        EXPECT_TRUE(Base.changed());
    }

    TEST_F(ThemeTokenTests, SetTheme_ATokenTheThemeDoesNotNameGoesBackToItsOriginal)
    {
        ThemeTokens::setTheme({ { "themetest.Base", text(OtherColor) }, { "themetest.Length", "9" } });
        ThemeTokens::setTheme({ { "themetest.Length", "9" } });

        EXPECT_EQ(QColor(Base), BaseOriginal);
        EXPECT_FALSE(Base.changed());
        EXPECT_EQ(static_cast<int>(Length), 9);
    }

    TEST_F(ThemeTokenTests, SetValue_ChangesOneToken_ClearValueTakesItBack)
    {
        EXPECT_TRUE(ThemeTokens::setValue("themetest.Length", "7"));
        EXPECT_EQ(static_cast<int>(Length), 7);

        ThemeTokens::clearValue("themetest.Length");
        EXPECT_EQ(static_cast<int>(Length), 4);
    }

    TEST_F(ThemeTokenTests, SetValue_RefusesATextThatIsNoValueOfTheKind)
    {
        EXPECT_FALSE(ThemeTokens::setValue("themetest.Length", "wide"));
        EXPECT_FALSE(ThemeTokens::setValue("themetest.Base", "12"));
        EXPECT_FALSE(ThemeTokens::setValue("themetest.Margins", "1 2 3"));
        EXPECT_EQ(static_cast<int>(Length), 4);
        EXPECT_EQ(QColor(Base), BaseOriginal);
    }

    TEST_F(ThemeTokenTests, SetTheme_ReturnsTheKeysWhoseTextIsNoValue_AndLeavesThoseTokens)
    {
        const QVector<QByteArray> refused =
            ThemeTokens::setTheme({ { "themetest.Length", "wide" }, { "themetest.Base", text(OtherColor) } });

        EXPECT_EQ(refused, QVector<QByteArray>{ "themetest.Length" });
        EXPECT_EQ(static_cast<int>(Length), 4);
        EXPECT_EQ(QColor(Base), OtherColor);
    }

    // ----------------------------------------------------------------------------------
    // T2: followers
    // ----------------------------------------------------------------------------------

    TEST_F(ThemeTokenTests, Follower_HasItsSourcesValueWhileNoThemeSetsIt)
    {
        ThemeTokens::setTheme({ { "themetest.Base", text(OtherColor) } });

        EXPECT_EQ(QColor(Follower), OtherColor);
        EXPECT_EQ(QColor(SecondFollower), OtherColor);
        EXPECT_TRUE(Follower.changed());
    }

    TEST_F(ThemeTokenTests, Follower_KeepsAValueOfItsOwn)
    {
        const QColor own(0x01, 0x02, 0x03);
        ThemeTokens::setTheme({ { "themetest.Base", text(OtherColor) }, { "themetest.Follower", text(own) } });

        EXPECT_EQ(QColor(Follower), own);
        // The follower of the follower takes the follower's value, not the base's
        EXPECT_EQ(QColor(SecondFollower), own);
    }

    TEST_F(ThemeTokenTests, Info_NamesTheSourceAndTheOriginalOfAFollower)
    {
        const ThemeTokens::Info info = ThemeTokens::info("themetest.SecondFollower");

        EXPECT_EQ(info.m_source, QByteArray("themetest.Follower"));
        EXPECT_EQ(info.m_original.value<QColor>(), BaseOriginal);
        EXPECT_EQ(info.m_kind, ThemeKind::Color);
        EXPECT_EQ(info.m_level, ThemeLevel::Advanced);
    }

    TEST_F(ThemeTokenTests, Info_HasTheSentenceTheDeclarationGives)
    {
        EXPECT_EQ(ThemeTokens::info("themetest.Described").m_description, QStringLiteral("Where it shows"));
        EXPECT_TRUE(ThemeTokens::info("themetest.Base").m_description.isEmpty());
        // The tokens a scheme starts from say where they show
        EXPECT_FALSE(ThemeTokens::info("tier.Mid").m_description.isEmpty());
    }

    TEST_F(ThemeTokenTests, Theme_ADollarKeyFollowsTheTokenOfThatKey)
    {
        const QColor own(0x01, 0x02, 0x03);
        ThemeTokens::setTheme({ { "themetest.Follower", text(own) }, { "themetest.Base", "$themetest.Follower" } });

        EXPECT_EQ(QColor(Base), own);
    }

    TEST_F(ThemeTokenTests, Theme_ARingOfDollarKeysEndsAtTheOriginal)
    {
        ThemeTokens::setTheme({ { "themetest.Base", "$themetest.Follower" }, { "themetest.Follower", "$themetest.Base" } });

        EXPECT_EQ(QColor(Base), BaseOriginal);
        EXPECT_EQ(QColor(Follower), BaseOriginal);
    }

    TEST_F(ThemeTokenTests, Theme_ADollarKeyOfAnotherKindIsRefused)
    {
        EXPECT_FALSE(ThemeTokens::setValue("themetest.Base", "$themetest.Length"));
        EXPECT_FALSE(ThemeTokens::setValue("themetest.Base", "$themetest.NoSuchToken"));
    }

    // ----------------------------------------------------------------------------------
    // T3: what reads a token
    // ----------------------------------------------------------------------------------

    TEST_F(ThemeTokenTests, KeyOf_NamesTheTokenAReferenceReads_AndNothingElse)
    {
        const QColor copy = Base;
        EXPECT_EQ(ThemeTokens::keyOf(&BaseName), QByteArray("themetest.Base"));
        EXPECT_TRUE(ThemeTokens::keyOf(&copy).isEmpty());
        EXPECT_TRUE(ThemeTokens::keyOf(nullptr).isEmpty());
    }

    TEST_F(ThemeTokenTests, Reference_ReadsTheTokenItself)
    {
        ThemeTokens::setTheme({ { "themetest.Base", text(OtherColor) }, { "themetest.Length", "9" } });

        EXPECT_EQ(BaseName, OtherColor);
        EXPECT_EQ(LengthName, 9);
    }

    TEST_F(ThemeTokenTests, Themed_KeepsItsValueUntilRebuild_ThenHasTheThemes)
    {
        ThemeTokens::setTheme({ { "themetest.Base", text(OtherColor) }, { "themetest.Length", "9" } });
        EXPECT_EQ(BaseBrush.color(), BaseOriginal);
        EXPECT_EQ(static_cast<int>(TwiceTheLength), 8);

        ThemeTokens::rebuild();

        EXPECT_EQ(BaseBrush.color(), OtherColor);
        EXPECT_EQ(static_cast<int>(TwiceTheLength), 18);
    }

    TEST_F(ThemeTokenTests, Themed_AValueThatReadsAnotherIsBuiltAfterIt)
    {
        ThemeTokens::setTheme({ { "themetest.Length", "9" } });
        ThemeTokens::rebuild();

        EXPECT_EQ(static_cast<int>(FourTimesTheLength), 36);
    }

    TEST_F(ThemeTokenTests, Themed_StaysAtItsAddress)
    {
        const QBrush* before = &BaseBrush;
        ThemeTokens::setTheme({ { "themetest.Base", text(OtherColor) } });
        ThemeTokens::rebuild();

        EXPECT_EQ(&BaseBrush, before);
    }

    TEST_F(ThemeTokenTests, Text_APointerReadBeforeAThemeChangeStaysValid)
    {
        ThemeTokens::setTheme({ { "themetest.Family", "Arial" } });
        const char* arial = Family;
        ThemeTokens::setTheme({ { "themetest.Family", "Consolas" } });
        ThemeTokens::setTheme({});

        EXPECT_STREQ(arial, "Arial");
        EXPECT_STREQ(Family, "Open Sans");
    }

    // ----------------------------------------------------------------------------------
    // T4: the copies of a token (one per module on Windows), and tokens that list themselves late
    // ----------------------------------------------------------------------------------

    TEST_F(ThemeTokenTests, Copy_ASecondTokenOfAKeyTakesTheThemesValueWhenItIsBuilt)
    {
        ThemeTokens::setTheme({ { "themetest.Base", text(OtherColor) } });

        const ThemeColor copy{ BaseOriginal, "themetest.Base", ThemeLevel::Basic, "Base" };

        EXPECT_EQ(QColor(copy), OtherColor);
        EXPECT_TRUE(copy.changed());
    }

    TEST_F(ThemeTokenTests, Copy_EveryCopyOfAKeyIsWritten_AndACopyThatIsGoneIsNot)
    {
        {
            const ThemeColor copy{ BaseOriginal, "themetest.Base", ThemeLevel::Basic, "Base" };
            ThemeTokens::setTheme({ { "themetest.Base", text(OtherColor) } });
            EXPECT_EQ(QColor(copy), OtherColor);
            EXPECT_EQ(QColor(Base), OtherColor);
        }
        // The copy is destroyed: writing the tokens again must not touch it
        ThemeTokens::setTheme({});
        EXPECT_EQ(QColor(Base), BaseOriginal);
    }

    TEST_F(ThemeTokenTests, Copy_TheOriginalOfAKeyIsItsFirstCopys_EvenUnderATheme)
    {
        ThemeTokens::setTheme({ { "themetest.Base", text(OtherColor) } });
        const ThemeColor copy{ BaseOriginal, "themetest.Base", ThemeLevel::Basic, "Base" };

        EXPECT_EQ(ThemeTokens::info("themetest.Base").m_original.value<QColor>(), BaseOriginal);
    }

    TEST_F(ThemeTokenTests, LateToken_AKeyTheThemeNamedBeforeItsTokenExistedApplies)
    {
        ThemeTokens::setTheme({ { "themetest.Late", text(OtherColor) } });
        EXPECT_FALSE(ThemeTokens::contains("themetest.Late"));

        const ThemeColor late{ BaseOriginal, "themetest.Late", ThemeLevel::Advanced, "Late" };

        EXPECT_EQ(QColor(late), OtherColor);
        EXPECT_EQ(ThemeTokens::info("themetest.Late").m_original.value<QColor>(), BaseOriginal);
    }

    // ----------------------------------------------------------------------------------
    // T5: values as text
    // ----------------------------------------------------------------------------------

    TEST_F(ThemeTokenTests, Text_AColorIsRRGGBB_AndRRGGBBAAWhereItIsTranslucent)
    {
        EXPECT_EQ(text(QColor(0x1E, 0x70, 0xEB)), QStringLiteral("#1E70EB"));
        EXPECT_EQ(text(QColor(0xFF, 0xFF, 0xFF, 0x33)), QStringLiteral("#FFFFFF33"));

        EXPECT_EQ(ThemeTokens::fromText(ThemeKind::Color, "#1e70eb").value<QColor>(), QColor(0x1E, 0x70, 0xEB));
        EXPECT_EQ(ThemeTokens::fromText(ThemeKind::Color, "#FFFFFF33").value<QColor>(), QColor(0xFF, 0xFF, 0xFF, 0x33));
        EXPECT_EQ(ThemeTokens::fromText(ThemeKind::Color, "#abc").value<QColor>(), QColor(0xAA, 0xBB, 0xCC));
    }

    TEST_F(ThemeTokenTests, Text_WhatIsNoValueIsInvalid)
    {
        EXPECT_FALSE(ThemeTokens::fromText(ThemeKind::Color, "1E70EB").isValid());
        EXPECT_FALSE(ThemeTokens::fromText(ThemeKind::Color, "#1E70E").isValid());
        EXPECT_FALSE(ThemeTokens::fromText(ThemeKind::Color, "#GGGGGG").isValid());
        EXPECT_FALSE(ThemeTokens::fromText(ThemeKind::Color, "#1E70EBZZ").isValid());
        EXPECT_FALSE(ThemeTokens::fromText(ThemeKind::Int, "1.5").isValid());
        EXPECT_FALSE(ThemeTokens::fromText(ThemeKind::Margins, "1 2").isValid());
        EXPECT_FALSE(ThemeTokens::fromText(ThemeKind::Text, "  ").isValid());
    }

    TEST_F(ThemeTokenTests, Text_MarginsAreFourLengths_OrOneForEverySide)
    {
        EXPECT_EQ(ThemeTokens::fromText(ThemeKind::Margins, "1 2 3 4").value<QMargins>(), QMargins(1, 2, 3, 4));
        EXPECT_EQ(ThemeTokens::fromText(ThemeKind::Margins, "-2").value<QMargins>(), QMargins(-2, -2, -2, -2));
        EXPECT_EQ(ThemeTokens::toText(ThemeKind::Margins, QVariant::fromValue(QMargins(1, 2, 3, 4))), QStringLiteral("1 2 3 4"));
    }

    // ----------------------------------------------------------------------------------
    // T6: the root catalogue
    // ----------------------------------------------------------------------------------

    TEST_F(ThemeTokenTests, Foundations_EveryTokenHasAnOriginalALabelAndAGroupWithATitle)
    {
        QSet<QByteArray> described;
        for (const ThemeTokens::Group& group : ThemeTokens::groups())
        {
            described.insert(group.m_key);
            EXPECT_FALSE(group.m_title.isEmpty()) << group.m_key.constData();
        }
        int count = 0;
        for (const ThemeTokens::Info& token : ThemeTokens::all())
        {
            if (token.m_key.startsWith("themetest."))
            {
                continue;
            }
            ++count;
            EXPECT_TRUE(token.m_original.isValid()) << token.m_key.constData();
            EXPECT_FALSE(token.m_label.isEmpty()) << token.m_key.constData();
            EXPECT_TRUE(described.contains(token.m_key.left(token.m_key.indexOf('.')))) << token.m_key.constData();
            EXPECT_TRUE(token.m_source.isEmpty() || ThemeTokens::contains(token.m_source)) << token.m_key.constData();
        }
        EXPECT_GT(count, 60);
    }

    TEST_F(ThemeTokenTests, Foundations_OriginalIsWhatTheLegacySheetsGranted)
    {
        // A sample of the catalogue against the values the flattening located (Foundations.md)
        EXPECT_EQ(QColor(Foundations::FarBG), QColor(0x11, 0x11, 0x11));
        EXPECT_EQ(QColor(Foundations::Mid), QColor(0x44, 0x44, 0x44));
        EXPECT_EQ(QColor(Foundations::Accent::Primary), QColor(0x1E, 0x70, 0xEB));
        EXPECT_EQ(QColor(Foundations::Field::Fill), QColor(0xCC, 0xCC, 0xCC));
        EXPECT_EQ(QColor(Foundations::Field::HoverLine), QColor(0x22, 0x22, 0x22));
        EXPECT_EQ(QColor(Foundations::Surface::Text), QColor(0xFF, 0xFF, 0xFF));
        EXPECT_EQ(static_cast<int>(Foundations::Field::Radius), 2);
        EXPECT_EQ(static_cast<int>(Foundations::Label::Size), 12);
        EXPECT_EQ(QMargins(Foundations::Field::Margin), QMargins(2, 2, 2, 2));
        EXPECT_STREQ(Foundations::Field::Family, "Open Sans");
    }

    TEST_F(ThemeTokenTests, Foundations_AFragmentFollowsItsTier)
    {
        ThemeTokens::setTheme({ { "tier.BG", text(OtherColor) } });

        EXPECT_EQ(QColor(Foundations::Field::HoverLine), OtherColor);
        EXPECT_EQ(QColor(Foundations::Line::Segment), OtherColor);
    }

    // ----------------------------------------------------------------------------------
    // Fixture: a manager over two folders of its own, and an application for it to polish
    // ----------------------------------------------------------------------------------

    class ThemeManagerTests
        : public ::testing::Test
    {
    public:
        ThemeManagerTests()
        {
            m_application = AZStd::make_unique<QApplication>(m_argc, m_argv);
            m_builtIn = m_folder.filePath("engine");
            m_user = m_folder.filePath("user");
            QDir().mkpath(m_builtIn);
            write(m_builtIn, "Steel", "", { { "themetest.Base", text(OtherColor) }, { "themetest.Length", "9" } });
            m_manager = AZStd::make_unique<ThemeManager>(m_builtIn, m_user, nullptr);
        }

        ~ThemeManagerTests() override
        {
            m_manager.reset();
            ThemeTokens::setTheme({});
            ThemeTokens::rebuild();
            m_application.reset();
        }

        void write(const QString& folder, const QString& name, const QString& base, const QMap<QByteArray, QString>& values)
        {
            ThemeFile theme;
            theme.m_name = name;
            theme.m_base = base;
            theme.m_values = values;
            EXPECT_TRUE(theme.write(QDir(folder).filePath(name + ".theme.json")));
        }

    protected:
        int m_argc = 1;                                     // QApplication keeps a reference to it
        char m_programName[5] = "test";
        char* m_argv[2] = { m_programName, nullptr };
        AZStd::unique_ptr<QApplication> m_application;
        QTemporaryDir m_folder;
        QString m_builtIn;
        QString m_user;
        AZStd::unique_ptr<ThemeManager> m_manager;
    };

    // ----------------------------------------------------------------------------------
    // T7: theme files
    // ----------------------------------------------------------------------------------

    TEST_F(ThemeManagerTests, File_ReadsBackWhatWasWritten)
    {
        ThemeFile written;
        written.m_name = "Round Trip";
        written.m_description = "A description";
        written.m_author = "A person";
        written.m_base = "Steel";
        written.m_values = { { "themetest.Base", "#010203" }, { "themetest.Length", "7" }, { "themetest.Margins", "1 2 3 4" },
                             { "themetest.Family", "Arial" }, { "themetest.Follower", "$themetest.Base" } };
        const QString path = m_folder.filePath("roundtrip.theme.json");
        ASSERT_TRUE(written.write(path));

        ThemeFile read;
        ASSERT_TRUE(ThemeFile::read(path, read));

        EXPECT_EQ(read.m_name, written.m_name);
        EXPECT_EQ(read.m_description, written.m_description);
        EXPECT_EQ(read.m_author, written.m_author);
        EXPECT_EQ(read.m_base, written.m_base);
        EXPECT_EQ(read.m_values, written.m_values);
    }

    TEST_F(ThemeManagerTests, File_ALengthIsANumber_AndFourNumbersAreMargins)
    {
        const QString path = m_folder.filePath("typed.theme.json");
        QFile file(path);
        ASSERT_TRUE(file.open(QIODevice::WriteOnly));
        file.write(R"({ "name": "Typed", "structure": { "themetest.Length": 7, "themetest.Margins": [1, 2, 3, 4] } })");
        file.close();

        ThemeFile read;
        ASSERT_TRUE(ThemeFile::read(path, read));

        EXPECT_EQ(read.m_values.value("themetest.Length"), QStringLiteral("7"));
        EXPECT_EQ(read.m_values.value("themetest.Margins"), QStringLiteral("1 2 3 4"));
        // No base named: the theme lies on O3DE Original
        EXPECT_TRUE(read.m_base.isEmpty());
    }

    TEST_F(ThemeManagerTests, File_WhatIsNoThemeIsRefusedWithAReason)
    {
        const QString path = m_folder.filePath("broken.theme.json");
        QFile file(path);
        ASSERT_TRUE(file.open(QIODevice::WriteOnly));
        file.write("{ \"colors\": { } }");
        file.close();

        ThemeFile read;
        QString error;
        EXPECT_FALSE(ThemeFile::read(path, read, &error));
        EXPECT_FALSE(error.isEmpty());
        EXPECT_FALSE(ThemeFile::read(m_folder.filePath("missing.theme.json"), read, &error));
    }

    // ----------------------------------------------------------------------------------
    // T8: choosing a theme
    // ----------------------------------------------------------------------------------

    TEST_F(ThemeManagerTests, Starts_WithOriginalAndTheEnginesThemes)
    {
        EXPECT_EQ(m_manager->activeTheme(), QString(ThemeManager::OriginalName));
        EXPECT_EQ(m_manager->themes(), (QStringList{ ThemeManager::OriginalName, "Steel" }));
        EXPECT_TRUE(m_manager->isBuiltIn("Steel"));
        EXPECT_EQ(QColor(Base), BaseOriginal);
    }

    TEST_F(ThemeManagerTests, SetActiveTheme_AppliesTheValuesAndBuildsTheThemedValuesAgain)
    {
        EXPECT_TRUE(m_manager->setActiveTheme("Steel"));

        EXPECT_EQ(QColor(Base), OtherColor);
        EXPECT_EQ(BaseBrush.color(), OtherColor);
        EXPECT_EQ(static_cast<int>(TwiceTheLength), 18);

        EXPECT_TRUE(m_manager->setActiveTheme(ThemeManager::OriginalName));
        EXPECT_EQ(QColor(Base), BaseOriginal);
        EXPECT_EQ(BaseBrush.color(), BaseOriginal);
    }

    TEST_F(ThemeManagerTests, SetActiveTheme_ANameNoThemeHasIsRefused)
    {
        EXPECT_FALSE(m_manager->setActiveTheme("No Such Theme"));
        EXPECT_EQ(m_manager->activeTheme(), QString(ThemeManager::OriginalName));
    }

    TEST_F(ThemeManagerTests, Choice_IsSavedAndRestored)
    {
        m_manager->setActiveTheme("Steel");
        m_manager.reset();
        ThemeTokens::setTheme({});

        m_manager = AZStd::make_unique<ThemeManager>(m_builtIn, m_user, nullptr);
        EXPECT_EQ(m_manager->activeTheme(), QString(ThemeManager::OriginalName));
        ThemeManager::restoreSavedTheme();

        EXPECT_EQ(m_manager->activeTheme(), QStringLiteral("Steel"));
        EXPECT_EQ(QColor(Base), OtherColor);
    }

    TEST_F(ThemeManagerTests, Base_AThemeHasTheValuesOfTheThemeBelowIt_AndItsOwnOverThem)
    {
        write(m_user, "Mine", "Steel", { { "themetest.Length", "11" } });
        m_manager->reload();

        m_manager->setActiveTheme("Mine");

        EXPECT_EQ(QColor(Base), OtherColor);
        EXPECT_EQ(static_cast<int>(Length), 11);
        EXPECT_FALSE(m_manager->isBuiltIn("Mine"));
    }

    // ----------------------------------------------------------------------------------
    // T9: editing
    // ----------------------------------------------------------------------------------

    TEST_F(ThemeManagerTests, SetValue_OnAnEngineThemeMakesAThemeOfTheUsersBasedOnIt)
    {
        m_manager->setActiveTheme("Steel");

        EXPECT_TRUE(m_manager->setValue("themetest.Length", "13"));

        const QString active = m_manager->activeTheme();
        EXPECT_NE(active, QStringLiteral("Steel"));
        EXPECT_FALSE(m_manager->isBuiltIn(active));
        EXPECT_EQ(m_manager->theme(active).m_base, QStringLiteral("Steel"));
        EXPECT_TRUE(m_manager->hasOwnValue("themetest.Length"));
        EXPECT_FALSE(m_manager->hasOwnValue("themetest.Base"));
        // The engine's theme is as it was, and the new one has its values below its own
        EXPECT_EQ(m_manager->theme("Steel").m_values.value("themetest.Length"), QStringLiteral("9"));
        EXPECT_EQ(static_cast<int>(Length), 13);
        EXPECT_EQ(QColor(Base), OtherColor);
    }

    TEST_F(ThemeManagerTests, SetValue_IsSavedToTheUsersThemeFile)
    {
        m_manager->setValue("themetest.Length", "13");
        const QString active = m_manager->activeTheme();
        const QString path = m_manager->theme(active).m_path;
        m_manager.reset();      // saves what is unsaved

        ThemeFile read;
        ASSERT_TRUE(ThemeFile::read(path, read));
        EXPECT_EQ(read.m_name, active);
        EXPECT_EQ(read.m_values.value("themetest.Length"), QStringLiteral("13"));
    }

    TEST_F(ThemeManagerTests, SetValue_ATextThatIsNoValueChangesNothing)
    {
        EXPECT_FALSE(m_manager->setValue("themetest.Length", "wide"));
        EXPECT_EQ(m_manager->activeTheme(), QString(ThemeManager::OriginalName));
        EXPECT_EQ(m_manager->themes().size(), 2);
    }

    TEST_F(ThemeManagerTests, ResetValue_BringsTheBaseThemesValueBack)
    {
        m_manager->setActiveTheme("Steel");
        m_manager->setValue("themetest.Length", "13");

        m_manager->resetValue("themetest.Length");

        EXPECT_EQ(static_cast<int>(Length), 9);
        EXPECT_FALSE(m_manager->hasOwnValue("themetest.Length"));
    }

    // ----------------------------------------------------------------------------------
    // T10: the user's themes
    // ----------------------------------------------------------------------------------

    TEST_F(ThemeManagerTests, Duplicate_OfAnEngineThemeLiesOverIt)
    {
        const QString made = m_manager->duplicate("Steel", "Steel copy");

        EXPECT_EQ(made, QStringLiteral("Steel copy"));
        EXPECT_EQ(m_manager->theme(made).m_base, QStringLiteral("Steel"));
        EXPECT_TRUE(QFile::exists(m_manager->theme(made).m_path));
        // A name that is taken gets a number
        EXPECT_EQ(m_manager->duplicate("Steel", "Steel copy"), QStringLiteral("Steel copy 2"));
    }

    TEST_F(ThemeManagerTests, Rename_MovesTheFileAndTheThemesThatLieOverIt)
    {
        const QString made = m_manager->duplicate("Steel", "First");
        write(m_user, "Over", "First", { { "themetest.Length", "11" } });
        m_manager->reload();
        const QString oldPath = m_manager->theme("First").m_path;

        EXPECT_TRUE(m_manager->rename("First", "Renamed"));

        EXPECT_FALSE(m_manager->contains("First"));
        EXPECT_FALSE(QFile::exists(oldPath));
        EXPECT_TRUE(QFile::exists(m_manager->theme("Renamed").m_path));
        EXPECT_EQ(m_manager->theme("Over").m_base, QStringLiteral("Renamed"));
        // An engine theme, a name that is taken, an empty name
        EXPECT_FALSE(m_manager->rename("Steel", "Other"));
        EXPECT_FALSE(m_manager->rename("Renamed", "Over"));
        EXPECT_FALSE(m_manager->rename("Renamed", " "));
    }

    TEST_F(ThemeManagerTests, Remove_TheThemeInEffectHandsOverToItsBase)
    {
        m_manager->setActiveTheme("Steel");
        m_manager->setValue("themetest.Length", "13");
        const QString mine = m_manager->activeTheme();
        const QString path = m_manager->theme(mine).m_path;

        EXPECT_TRUE(m_manager->remove(mine));

        EXPECT_EQ(m_manager->activeTheme(), QStringLiteral("Steel"));
        EXPECT_FALSE(QFile::exists(path));
        EXPECT_EQ(static_cast<int>(Length), 9);
        EXPECT_FALSE(m_manager->remove("Steel"));
    }

    TEST_F(ThemeManagerTests, Export_HoldsEveryValueTheThemeHasThroughItsBases_AndImportReadsItBack)
    {
        write(m_user, "Mine", "Steel", { { "themetest.Length", "11" } });
        m_manager->reload();
        const QString path = m_folder.filePath("exported.theme.json");

        ASSERT_TRUE(m_manager->exportTheme("Mine", path));
        ThemeFile exported;
        ASSERT_TRUE(ThemeFile::read(path, exported));
        EXPECT_EQ(exported.m_base, QStringLiteral("Steel"));
        EXPECT_EQ(exported.m_values.value("themetest.Length"), QStringLiteral("11"));
        EXPECT_EQ(exported.m_values.value("themetest.Base"), text(OtherColor));

        // The name is taken here: the imported theme gets a number
        const QString imported = m_manager->importTheme(path);
        EXPECT_EQ(imported, QStringLiteral("Mine 2"));
        EXPECT_FALSE(m_manager->isBuiltIn(imported));
    }

    TEST_F(ThemeManagerTests, SetDetails_DescribesOneOfTheUsersThemes_AndItsFileKeepsIt)
    {
        EXPECT_FALSE(m_manager->setDetails("Not mine", "Nobody"));      // O3DE Original is the engine's
        m_manager->setValue("themetest.Length", "13");
        const QString path = m_manager->theme(m_manager->activeTheme()).m_path;

        EXPECT_TRUE(m_manager->setDetails(" A description ", "A person"));
        m_manager.reset();      // saves what is unsaved

        ThemeFile read;
        ASSERT_TRUE(ThemeFile::read(path, read));
        EXPECT_EQ(read.m_description, QStringLiteral("A description"));
        EXPECT_EQ(read.m_author, QStringLiteral("A person"));
    }

    // ----------------------------------------------------------------------------------
    // T11: several values in one change, and a preview
    // ----------------------------------------------------------------------------------

    TEST_F(ThemeManagerTests, SetValues_GivesSeveralTokensValuesInOneChange_AndLeavesOutWhatIsNoValue)
    {
        const QVector<QByteArray> refused =
            m_manager->setValues({ { "themetest.Length", "21" }, { "themetest.Base", "#010203" }, { "themetest.Margins", "wide" } });

        EXPECT_EQ(refused, QVector<QByteArray>{ "themetest.Margins" });
        EXPECT_EQ(static_cast<int>(Length), 21);
        EXPECT_EQ(QColor(Base), QColor(1, 2, 3));
        EXPECT_TRUE(m_manager->hasOwnValue("themetest.Length"));
        EXPECT_FALSE(m_manager->hasOwnValue("themetest.Margins"));
        EXPECT_FALSE(m_manager->isBuiltIn(m_manager->activeTheme()));
    }

    TEST_F(ThemeManagerTests, Preview_ShowsValuesAndChangesNoTheme_EndPreviewShowsTheThemeAgain)
    {
        m_manager->setActiveTheme("Steel");

        m_manager->preview({ { "themetest.Length", "30" } });
        EXPECT_EQ(static_cast<int>(Length), 30);
        EXPECT_EQ(QColor(Base), OtherColor);                // the theme's other values stay
        EXPECT_EQ(m_manager->activeTheme(), QStringLiteral("Steel"));
        EXPECT_EQ(m_manager->themes().size(), 2);

        m_manager->endPreview();
        EXPECT_EQ(static_cast<int>(Length), 9);
    }

    TEST_F(ThemeManagerTests, Preview_AnEditEndsIt)
    {
        m_manager->preview({ { "themetest.Length", "30" } });

        m_manager->setValue("themetest.Base", "#010203");

        EXPECT_EQ(static_cast<int>(Length), 4);
        EXPECT_EQ(QColor(Base), QColor(1, 2, 3));
    }

    // ----------------------------------------------------------------------------------
    // T12: Recolor, a theme carried over to another background and accent
    // ----------------------------------------------------------------------------------

    namespace ThemeTestsInternal
    {
        const QColor Grey(0x44, 0x44, 0x44);            // the background of O3DE Original
        const QColor Blue(0x1E, 0x70, 0xEB);            // its accent
        const QColor Steel(0x28, 0x2B, 0x35);           // a tinted, darker background
        const QColor Orange(0xE0, 0x7B, 0x1A);          // another accent
    } // namespace ThemeTestsInternal

    TEST_F(ThemeTokenTests, Recolored_TheBackgroundAndTheAccentComeOutAsTheNewOnes)
    {
        EXPECT_EQ(ThemeEditor::recolored(Grey, Grey, Steel, Blue, Orange), Steel);
        EXPECT_EQ(ThemeEditor::recolored(Blue, Grey, Steel, Blue, Orange), Orange);
    }

    TEST_F(ThemeTokenTests, Recolored_AGreyTakesTheBackgroundsTintAndItsChangeOfLightness)
    {
        const QColor low = ThemeEditor::recolored(QColor(0x33, 0x33, 0x33), Grey, Steel, Blue, Blue);

        // Darker than the background as it was, and of its hue
        EXPECT_LT(low.lightnessF(), Steel.lightnessF());
        EXPECT_NEAR(low.hslHueF(), Steel.hslHueF(), 0.02f);
        // Black and white stay, and so does a colour of its own
        EXPECT_EQ(ThemeEditor::recolored(QColor(Qt::white), Grey, Steel, Blue, Orange), QColor(Qt::white));
        EXPECT_EQ(ThemeEditor::recolored(QColor(Qt::black), Grey, Steel, Blue, Orange), QColor(Qt::black));
        const QColor error(0xE2, 0x52, 0x43);
        EXPECT_EQ(ThemeEditor::recolored(error, Grey, Steel, Blue, Orange), error);
    }

    TEST_F(ThemeTokenTests, Recolored_AColourOfTheAccentsHueTurnsWithIt_AndStaysWhileTheAccentStays)
    {
        const QColor focus(0x94, 0xD2, 0xFF);           // a light blue near the accent's hue

        const QColor turned = ThemeEditor::recolored(focus, Grey, Grey, Blue, Orange);

        // As far from the new accent's hue as it was from the old one's
        EXPECT_NEAR(turned.hslHueF() - Orange.hslHueF(), focus.hslHueF() - Blue.hslHueF(), 0.02f);
        EXPECT_EQ(ThemeEditor::recolored(focus, Grey, Steel, Blue, Blue), focus);
        // A grey stays while the background stays
        EXPECT_EQ(ThemeEditor::recolored(QColor(0x33, 0x33, 0x33), Grey, Grey, Blue, Orange), QColor(0x33, 0x33, 0x33));
    }

    TEST_F(ThemeTokenTests, RecoloredTheme_NamesWhatChanges_AndLeavesTheFollowersToFollow)
    {
        const QHash<QByteArray, QString> values = ThemeEditor::recoloredTheme(ThemeTokens::all(), Steel, Blue);

        EXPECT_EQ(values.value("tier.Mid"), text(Steel));
        EXPECT_TRUE(values.contains("tier.Low"));
        EXPECT_FALSE(values.contains("accent.Primary"));     // the accent stays
        EXPECT_FALSE(values.contains("accent.Focus"));
        EXPECT_FALSE(values.contains("accent.Error"));       // a colour of its own
        EXPECT_FALSE(values.contains("text.Primary"));       // white
        EXPECT_FALSE(values.contains("menu.Fill"));          // follows tier.BG
    }

    // ----------------------------------------------------------------------------------
    // T13: the theme editor's rows
    // ----------------------------------------------------------------------------------

    TEST_F(ThemeManagerTests, Editor_ShowsTheTokensOfTheLevelChosen_AndASearchFindsTheOthers)
    {
        ThemeEditor editor;

        // Basic colours: the test's basic colour has a row, its advanced follower none
        EXPECT_NE(editor.findChild<QLineEdit*>("value:themetest.Base"), nullptr);
        EXPECT_EQ(editor.findChild<QLineEdit*>("value:themetest.Follower"), nullptr);

        auto* search = editor.findChild<QLineEdit*>("themeSearch");
        ASSERT_NE(search, nullptr);
        search->setText("Second follower");
        EXPECT_NE(editor.findChild<QLineEdit*>("value:themetest.SecondFollower"), nullptr);
        EXPECT_EQ(editor.findChild<QLineEdit*>("value:themetest.Base"), nullptr);
    }

    TEST_F(ThemeManagerTests, Editor_AnEditOfARowGoesToAThemeOfTheUsers)
    {
        ThemeEditor editor;
        auto* field = editor.findChild<QLineEdit*>("value:themetest.Base");
        ASSERT_NE(field, nullptr);
        EXPECT_EQ(field->text(), text(BaseOriginal));

        field->setText("a0b0c0");                     // typed without the #, in lower case
        emit field->editingFinished();

        EXPECT_EQ(QColor(Base), OtherColor);
        EXPECT_FALSE(m_manager->isBuiltIn(m_manager->activeTheme()));
        EXPECT_TRUE(m_manager->hasOwnValue("themetest.Base"));
        // The rows show the theme in effect: the field of the row, found again, has the value as the registry writes it
        field = editor.findChild<QLineEdit*>("value:themetest.Base");
        ASSERT_NE(field, nullptr);
        EXPECT_EQ(field->text(), text(OtherColor));
    }

    TEST_F(ThemeManagerTests, Editor_TheStructurePartEditsALength_AndChangedOnlyListsWhatTheThemeSets)
    {
        ThemeEditor editor;
        auto* parts = editor.findChild<QTabBar*>("themeParts");
        auto* level = editor.findChild<QComboBox*>("themeLevel");
        ASSERT_NE(parts, nullptr);
        ASSERT_NE(level, nullptr);
        parts->setCurrentIndex(1);
        level->setCurrentIndex(1);                      // Standard: the test's length
        emit level->activated(1);
        auto* length = editor.findChild<QSpinBox*>("value:themetest.Length");
        ASSERT_NE(length, nullptr);
        EXPECT_EQ(length->value(), 4);

        length->setValue(17);

        EXPECT_EQ(static_cast<int>(Length), 17);

        auto* changedOnly = editor.findChild<QAbstractButton*>("themeChangedOnly");
        ASSERT_NE(changedOnly, nullptr);
        changedOnly->setChecked(true);
        EXPECT_NE(editor.findChild<QSpinBox*>("value:themetest.Length"), nullptr);
        EXPECT_EQ(editor.findChild<QSpinBox*>("value:themetest.Margins"), nullptr);
    }
} // namespace AzQtComponents
