// The built plugin as Quillbinder loads it (QPluginLoader): the interface version, both interfaces, the commands,
// the inspector tab, the toolbar button with its menu, the options section, the window.
#include "FakeHost.h"

#include <QApplication>
#include <QMenu>
#include <QPluginLoader>
#include <QTemporaryDir>
#include <QtTest>

using namespace qb::tools;

class PluginTests : public QObject
{
    Q_OBJECT
private slots:
    void loads()
    {
        const QString file = QStringLiteral(TP_BUILD_DIR "/addon/plugins/libqb-translation-proofreading.so");
        QPluginLoader loader(file);
        QCOMPARE(loader.metaData().value(QStringLiteral("IID")).toString(), QStringLiteral(QB_TOOL_IID));
        QObject *o = loader.instance();
        QVERIFY2(o, qPrintable(loader.errorString()));
        auto *tool = qobject_cast<ToolInterface *>(o);
        auto *ext = qobject_cast<ToolExtension *>(o);
        QVERIFY(tool);
        QVERIFY(ext);
        QCOMPARE(tool->id(), QStringLiteral("translation-proofreading"));
        QVERIFY(!tool->version().isEmpty());

        QTemporaryDir data;
        FakeHost host(QStringLiteral(TP_BUILD_DIR "/addon/translation-proofreading"), data.path());
        tool->setHost(&host);
        QCoreApplication::processEvents();

        const QList<Command> commands = tool->commands();
        QCOMPARE(commands.size(), 2);
        for (const Command &c : commands) {
            QVERIFY(c.key.startsWith(QLatin1String("translation-proofreading.")));
            QCOMPARE(c.place, QStringLiteral("edit/writing-tools"));
            QVERIFY(!c.icon.isEmpty());
        }
        // the add-on's own icon reaches the menu entries
        QTRY_VERIFY(host.states.contains(commands.at(0).key));
        QVERIFY(host.states.value(commands.at(0).key).second.endsWith(QLatin1String("translation-proofreading.svg")));

        const QList<InspectorTab> tabs = ext->inspectorTabs();
        QCOMPARE(tabs.size(), 1);
        QVERIFY(QFileInfo::exists(tabs.at(0).icon));
        QWidget parent;
        QWidget *tab = ext->createInspectorTab(tabs.at(0).key, &parent);
        QVERIFY(tab);
        QCOMPARE(tab->objectName(), QStringLiteral("tpInspectorPanel"));
        QVERIFY(!ext->createInspectorTab(QStringLiteral("other"), &parent));

        const QList<ToolbarButton> buttons = ext->toolbarButtons();
        QCOMPARE(buttons.size(), 1);
        QMenu *menu = ext->commandMenu(buttons.at(0).commandKey);
        QVERIFY(menu);
        QVERIFY(menu->actions().size() >= 3);

        QCOMPARE(tool->optionsPage(), QStringLiteral("Section"));
        QVERIFY(!ext->optionsTitle().isEmpty());
        QWidget optParent;
        OptionsSection *section = tool->createOptions(&optParent);
        QVERIFY(section && section->widget());
        section->apply();
        delete section;   // the program deletes the section before the widgets (OptionsDialog)

        tool->run(commands.at(0).key);
        QCOMPARE(tool->openWindows(), 1);
        tool->run(commands.at(1).key);
        QCOMPARE(host.shownTabs.size(), 1);
        tool->shutdown();
        QCOMPARE(tool->openWindows(), 0);
    }
};

QTEST_MAIN(PluginTests)
#include "plugin_tests.moc"
