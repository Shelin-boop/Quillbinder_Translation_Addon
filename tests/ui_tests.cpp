// Checks of the add-on with the engine and real models, in a stand-in for Quillbinder (FakeHost): the options
// (download with checksums, a damaged file, the license question), the panel in the inspector and in the window,
// proofreading, translation, keeping an original, other words on both sides, the editor, the rules. Screenshots go
// into TP_SHOTS (default: build/screenshots).
//
// Needs the models' files in a folder (the release files, "<id>--<name>"): TP_MODELS_DIR. Without it the checks
// that need a model are skipped.
#include "FakeHost.h"
#include "OptionsPage.h"
#include "Panel.h"
#include "TranslateTool.h"

#include <QAbstractButton>
#include <QApplication>
#include <QComboBox>
#include <QElapsedTimer>
#include <QFile>
#include <QLabel>
#include <QMenu>
#include <QScrollArea>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTextDocument>
#include <QtTest>

using namespace tp;

namespace {

QString g_shots;

void shot(QWidget *w, const QString &name)
{
    QDir().mkpath(g_shots);
    w->grab().save(g_shots + QLatin1Char('/') + name + QStringLiteral(".png"));
}

const QString kEnglish = QStringLiteral(
    "The rain had not stopped for three days, and Mara was starting to loose her patience. She walked to the window "
    "and looked at the grey streets bellow, where a few people was hurrying home under there umbrellas.\n"
    "\"I dont think he is coming,\" she said quietly, more to herself then to anyone else.\n"
    "Behind her, the old clock ticked on — slow and indifferent. Her brother, who had travelled all the way from "
    "the coast, were supposed to arrive at noon. It was now almost six.\n"
    "She realised that she had been holding her breath. Their was nothing to do but wait.");

const QString kClean = QStringLiteral(
    "Night. The harbor lay still, and the boats knocked against the pier like impatient guests.\n"
    "Jonas didn’t answer. He never did, not when it mattered.\n"
    "“Come on,” she whispered. “Before they wake up.”");

const QString kGerman = QStringLiteral(
    "Der Regen hatte seit drei Tagen nicht aufgehört, und Mara verlor langsam die Geduld. Sie ging zum Fenster "
    "und sah hinunter auf die grauen Straßen, wo ein paar Menschen unter ihren Schirmen nach Hause eilten.\n"
    "Hinter ihr tickte die alte Uhr weiter, langsam und gleichgültig. Ihr Bruder sollte um zwölf Uhr "
    "ankommen. Jetzt war es fast sechs.");

int positionOf(QTextDocument *doc, const QString &word)
{
    const QTextCursor c = doc->find(word);
    return c.isNull() ? -1 : c.selectionStart();
}

} // namespace

class UiTests : public QObject
{
    Q_OBJECT
private:
    QTemporaryDir m_data;
    FakeHost *m_host = nullptr;
    TranslateTool *m_tool = nullptr;
    QString m_models;
    QWidget *m_optionsParent = nullptr;
    OptionsPage *m_options = nullptr;
    Panel *m_inspector = nullptr;

    bool haveModels() const { return !m_models.isEmpty() && QFileInfo::exists(m_models + QStringLiteral("/catalog.json")); }
    bool runPanel(Panel *p, int timeoutMs = 300000)
    {
        QSignalSpy done(p, &Panel::finished);
        p->run();
        const bool ok = done.count() > 0 || done.wait(timeoutMs);
        qInfo("status: %s", qPrintable(p->status()));
        return ok;
    }

private slots:
    void initTestCase()
    {
        m_models = qEnvironmentVariable("TP_MODELS_DIR");
        qputenv("QB_TP_DIR", QByteArray(TP_BUILD_DIR "/addon/translation-proofreading"));
        qputenv("QB_TP_MODEL_SOURCE", m_models.toUtf8());
        qputenv("QB_TP_ACCEPT_LICENSES", "1");
        m_host = new FakeHost(QStringLiteral(TP_BUILD_DIR "/addon/translation-proofreading"), m_data.path());
        m_tool = new TranslateTool;
        m_tool->setHost(m_host);
        QCoreApplication::processEvents();
    }

    void cleanupTestCase()
    {
        delete m_options;
        delete m_optionsParent;
        delete m_tool;
        delete m_host;
    }

    void commandsAndIcon()
    {
        const QList<qb::tools::Command> c = m_tool->commands();
        QCOMPARE(c.size(), 2);
        QCOMPARE(c.at(0).place, QStringLiteral("edit/writing-tools"));
        QCOMPARE(m_tool->optionsPage(), QStringLiteral("Section"));
        QVERIFY(QFileInfo::exists(m_tool->iconPath()));
        QTRY_VERIFY(m_host->states.contains(TranslateTool::kWindow));
        QCOMPARE(m_host->states.value(TranslateTool::kWindow).second, m_tool->iconPath());
        QCOMPARE(m_tool->inspectorTabs().value(0).icon, m_tool->iconPath());
        QVERIFY(m_tool->commandMenu(TranslateTool::kWindow));
        QCOMPARE(m_tool->commandMenu(TranslateTool::kWindow)->actions().size(), 4);
    }

    void noModelYet()
    {
        Panel p(m_tool, Panel::Place::Inspector, nullptr);
        p.setInput(kEnglish);
        p.run();
        QVERIFY(!p.isBusy());
        QVERIFY2(p.status().contains(QStringLiteral("No proofreading model")), qPrintable(p.status()));
        p.setInput(QString());
        p.run();
        QVERIFY(p.status().contains(QStringLiteral("no text")));
    }

    void damagedDownload()
    {
        if (!haveModels())
            QSKIP("TP_MODELS_DIR not set");
        // a source whose config.json of the translator is changed: the download must fail and install nothing
        QTemporaryDir bad;
        for (const QString &f : QDir(m_models).entryList(QDir::Files))
            if (f.startsWith(QLatin1String("opus-mt-de-en--")))
                QVERIFY(QFile::link(m_models + QLatin1Char('/') + f, bad.path() + QLatin1Char('/') + f));
        QFile::remove(bad.path() + QStringLiteral("/opus-mt-de-en--config.json"));
        QFile cfg(bad.path() + QStringLiteral("/opus-mt-de-en--config.json"));
        QVERIFY(cfg.open(QIODevice::WriteOnly));
        QFile orig(m_models + QStringLiteral("/opus-mt-de-en--config.json"));
        QVERIFY(orig.open(QIODevice::ReadOnly));
        QByteArray data = orig.readAll();
        data[10] = data[10] == 'x' ? 'y' : 'x';
        cfg.write(data);
        cfg.close();
        qputenv("QB_TP_MODEL_SOURCE", bad.path().toUtf8());
        QSignalSpy finished(m_tool->store(), &ModelStore::finished);
        m_tool->store()->download(QStringLiteral("opus-mt-de-en"));
        QVERIFY(finished.wait(120000));
        QCOMPARE(finished.last().at(1).toBool(), false);
        QVERIFY2(finished.last().at(2).toString().contains(QStringLiteral("checksum")), qPrintable(finished.last().at(2).toString()));
        QVERIFY(!m_tool->store()->isInstalled(QStringLiteral("opus-mt-de-en")));
        qputenv("QB_TP_MODEL_SOURCE", m_models.toUtf8());
    }

    void optionsAndDownload()
    {
        m_optionsParent = new QWidget;
        m_options = new OptionsPage(m_tool, m_optionsParent);
        auto *scroll = new QScrollArea(m_optionsParent);
        scroll->setWidgetResizable(true);
        scroll->setWidget(m_options->widget());
        m_optionsParent->resize(760, 1100);
        scroll->resize(760, 1100);
        m_optionsParent->show();
        shot(m_optionsParent, QStringLiteral("options-before"));
        if (!haveModels())
            QSKIP("TP_MODELS_DIR not set");
        QSignalSpy finished(m_tool->store(), &ModelStore::finished);
        // the translator first (the files already checked in damagedDownload are kept, the bad one is fetched again)
        m_options->download(QStringLiteral("opus-mt-de-en"));
        QVERIFY(finished.wait(300000));
        QVERIFY2(finished.last().at(1).toBool(), qPrintable(finished.last().at(2).toString()));
        m_options->download(QStringLiteral("coedit-large"));
        QVERIFY(finished.wait(600000));
        QVERIFY2(finished.last().at(1).toBool(), qPrintable(finished.last().at(2).toString()));
        QVERIFY(m_tool->store()->isInstalled(QStringLiteral("coedit-large")));
        QVERIFY(m_tool->store()->isInstalled(QStringLiteral("opus-mt-de-en")));
        QVERIFY(!m_tool->store()->isInstalled(QStringLiteral("coedit-base")));
        QCOMPARE(m_host->settings.value(QStringLiteral("translation-proofreading/accepted/coedit-large")).toBool(), true);
        m_options->apply();
        QCOMPARE(m_tool->chosenModel(QStringLiteral("corrector")), QStringLiteral("coedit-large"));
        QCOMPARE(m_tool->chosenModel(QStringLiteral("translator")), QStringLiteral("opus-mt-de-en"));
        QCoreApplication::processEvents();
        shot(m_optionsParent, QStringLiteral("options-downloaded"));
    }

    void proofreadInInspector()
    {
        if (!haveModels())
            QSKIP("TP_MODELS_DIR not set");
        m_inspector = qobject_cast<Panel *>(m_tool->createInspectorTab(TranslateTool::kTab, m_host->main));
        QVERIFY(m_inspector);
        m_inspector->resize(380, 900);
        m_inspector->show();
        m_inspector->setInput(kEnglish);
        QElapsedTimer t;
        t.start();
        QVERIFY(runPanel(m_inspector));
        qInfo("proofreading %lld words: %.1f s", qlonglong(words(kEnglish).size()), t.elapsed() / 1000.0);
        const QString r = m_inspector->resultText();
        qInfo("%s", qPrintable(r));
        for (const char *expected : {"lose her patience", "people were hurrying", "their umbrellas",
                                     "“I don’t think", "had traveled", "was supposed", "She realized",
                                     "There was nothing", "ticked on, slow"})
            QVERIFY2(r.contains(QString::fromUtf8(expected)), expected);
        // "bellow" is corrected; to "below" or (beam search with oneDNN's numbers, 2026-11-15) "downstairs"
        QVERIFY(!r.contains(QStringLiteral("bellow")));
        QVERIFY(!r.contains(QChar(0x2014)));
        QVERIFY(m_inspector->changeCount() >= 8);
        QVERIFY2(m_inspector->status().contains(QStringLiteral("change")), qPrintable(m_inspector->status()));
        QVERIFY(m_inspector->notes().contains(QStringLiteral("dash")));
        // the paragraphs stay
        QCOMPARE(r.count(QLatin1Char('\n')), kEnglish.count(QLatin1Char('\n')));
        shot(m_inspector, QStringLiteral("inspector-proofread"));
    }

    void keepOriginal()
    {
        if (!m_inspector)
            QSKIP("no result");
        QTextDocument *doc = m_inspector->output()->document();
        const int pos = positionOf(doc, QStringLiteral("lose"));
        QVERIFY(pos >= 0);
        const Panel::WordInfo w = m_inspector->wordAt(pos);
        QVERIFY(w.unit >= 0 && w.segment >= 0);
        QCOMPARE(w.text, QStringLiteral("lose"));
        m_inspector->setSegmentAccepted(w.unit, w.segment, false);
        QVERIFY(m_inspector->resultText().contains(QStringLiteral("to loose her")));
        m_inspector->setSegmentAccepted(w.unit, w.segment, true);
        QVERIFY(m_inspector->resultText().contains(QStringLiteral("to lose her")));
        m_inspector->setAllAccepted(false);
        QVERIFY(m_inspector->resultText().contains(QStringLiteral("there umbrellas")));
        m_inspector->setAllAccepted(true);
    }

    void wordMenuAndAlternatives()
    {
        if (!m_inspector)
            QSKIP("no result");
        QTextDocument *doc = m_inspector->output()->document();
        const int pos = positionOf(doc, QStringLiteral("patience."));
        const Panel::WordInfo w = m_inspector->wordAt(pos);
        QVERIFY(w.word >= 0);
        QString error;
        QElapsedTimer t;
        t.start();
        const QList<Alternative> alts = m_inspector->outputAlternatives(w.unit, w.word, &error);
        qInfo("alternatives for \"%s\" in %.2f s:", qPrintable(w.text), t.elapsed() / 1000.0);
        for (const Alternative &a : alts)
            qInfo("  %s | %s", qPrintable(a.word), qPrintable(a.sentence));
        QVERIFY2(!alts.isEmpty(), qPrintable(error));
        QVERIFY(alts.size() <= 6);
        const QString before = m_inspector->resultText();
        m_inspector->chooseOutputAlternative(w.unit, alts.first());
        QVERIFY(m_inspector->resultText() != before);
        QVERIFY(m_inspector->resultText().contains(alts.first().word));

        // the menu as the writer sees it: a click on a changed word
        int changedPos = positionOf(doc, QStringLiteral("below,"));
        if (changedPos < 0)
            changedPos = positionOf(doc, QStringLiteral("downstairs,"));
        QTextCursor c(doc);
        c.setPosition(changedPos + 1);
        const QRect a = m_inspector->output()->cursorRect(c);
        c.setPosition(changedPos + 2);
        const QRect b = m_inspector->output()->cursorRect(c);
        const QPoint at((a.left() + b.left()) / 2, a.center().y());   // in the middle of the second letter
        qInfo("click at %d,%d: char %d (word at %d)", at.x(), at.y(), m_inspector->output()->charAt(at), changedPos);
        QTest::mouseClick(m_inspector->output()->viewport(), Qt::LeftButton, {}, at);
        QMenu *menu = nullptr;
        QTRY_VERIFY((menu = m_inspector->findChild<QMenu *>(QStringLiteral("tpWordMenu"))) != nullptr);
        QVERIFY(menu->actions().size() >= 3);
        QVERIFY(menu->actions().at(0)->text().contains(QStringLiteral("Use")));
        QVERIFY(menu->actions().at(1)->text().contains(QStringLiteral("bellow")));
        // the other words come while the menu is open
        QTRY_VERIFY_WITH_TIMEOUT(menu->actions().last()->isEnabled() || menu->actions().last()->text().startsWith(QStringLiteral("No")), 60000);
        qInfo("word menu:");
        for (QAction *a : menu->actions())
            qInfo("  %s", qPrintable(a->text()));
        shot(menu, QStringLiteral("word-menu"));
        menu->close();
    }

    void cleanTextStays()
    {
        if (!haveModels())
            QSKIP("TP_MODELS_DIR not set");
        Panel p(m_tool, Panel::Place::Inspector, nullptr);
        p.setInput(kClean);
        QVERIFY(runPanel(&p));
        qInfo("%s\nnotes: %s", qPrintable(p.resultText()), qPrintable(p.notes()));
        // what the model got wrong in the test of 2026-11-15 must not get through
        QVERIFY(!p.resultText().contains(QStringLiteral("The night.")));
        QVERIFY(!p.resultText().contains(QStringLiteral("didn’t didn’t")));
        QVERIFY(p.resultText().contains(QStringLiteral("Jonas didn’t answer.")));
        QVERIFY(p.resultText().contains(QStringLiteral("“Come on,” she whispered.")));
    }

    void translate()
    {
        if (!haveModels())
            QSKIP("TP_MODELS_DIR not set");
        Panel p(m_tool, Panel::Place::Inspector, nullptr);
        p.resize(380, 800);
        p.setInput(kGerman);
        QElapsedTimer t;
        t.start();
        QVERIFY(runPanel(&p));
        qInfo("translation: %.1f s\n%s", t.elapsed() / 1000.0, qPrintable(p.resultText()));
        const QString r = p.resultText();
        QVERIFY(r.contains(QStringLiteral("rain")));
        QVERIFY(r.contains(QStringLiteral("brother")));
        QVERIFY(r.contains(QStringLiteral("gray")));   // American spelling also for the translation
        QCOMPARE(p.changeCount(), 0);
        QVERIFY(p.status().startsWith(QStringLiteral("Translated")));
        // other words for "Your"/"Her" (the start of a sentence)
        QTextDocument *doc = p.output()->document();
        int pos = positionOf(doc, QStringLiteral("brother"));
        QVERIFY(pos > 0);
        const Panel::WordInfo w = p.wordAt(pos);
        QVERIFY(w.word >= 0);
        QString error;
        const QList<Alternative> alts = p.outputAlternatives(w.unit, w.word, &error);
        qInfo("alternatives for \"%s\":", qPrintable(w.text));
        for (const Alternative &a : alts)
            qInfo("  %s | %s", qPrintable(a.word), qPrintable(a.sentence));
        QVERIFY2(!alts.isEmpty(), qPrintable(error));
        shot(&p, QStringLiteral("inspector-translated"));
    }

    void inputAlternatives()
    {
        if (!haveModels())
            QSKIP("TP_MODELS_DIR not set");
        Panel p(m_tool, Panel::Place::Inspector, nullptr);
        p.setInput(QStringLiteral("She walked to the window and looked at the gray streets below."));
        const int pos = p.inputText().indexOf(QStringLiteral("looked")) + 2;
        const Panel::InputPlace place = p.inputPlace(pos);
        QCOMPARE(place.wordText, QStringLiteral("looked"));
        QString error;
        const QList<Alternative> alts = p.inputAlternatives(place, &error);
        qInfo("input alternatives for \"looked\":");
        for (const Alternative &a : alts)
            qInfo("  %s | %s", qPrintable(a.word), qPrintable(a.sentence));
        QVERIFY2(!alts.isEmpty(), qPrintable(error));
        p.chooseInputAlternative(place, alts.first());
        QVERIFY(p.inputText().contains(alts.first().word));
        // German input offers none
        p.setInput(QStringLiteral("Sie ging zum Fenster und sah hinunter."));
        QVERIFY(p.inputPlace(5).wordText.isEmpty());
    }

    void editorRoundTrip()
    {
        if (!haveModels())
            QSKIP("TP_MODELS_DIR not set");
        Panel p(m_tool, Panel::Place::Inspector, nullptr);
        m_host->editor->setPlainText(QStringLiteral("Before. Their was nothing to do but wait. After."));
        QTextCursor c = m_host->editor->textCursor();
        c.setPosition(8);
        c.setPosition(41, QTextCursor::KeepAnchor);
        m_host->editor->setTextCursor(c);
        QCOMPARE(m_host->selectedText(), QStringLiteral("Their was nothing to do but wait."));
        QSignalSpy done(&p, &Panel::finished);
        p.takeFromEditor();
        QVERIFY(done.wait(120000));
        QCOMPARE(p.resultText(), QStringLiteral("There was nothing to do but wait."));
        p.putIntoEditor();
        QCOMPARE(m_host->editor->toPlainText(), QStringLiteral("Before. There was nothing to do but wait. After."));
        m_host->editor->undo();
        QCOMPARE(m_host->editor->toPlainText(), QStringLiteral("Before. Their was nothing to do but wait. After."));
    }

    void ownRules()
    {
        if (!haveModels())
            QSKIP("TP_MODELS_DIR not set");
        m_host->settings.insert(QStringLiteral("translation-proofreading/replacements"), QStringLiteral("OK => okay"));
        Panel p(m_tool, Panel::Place::Inspector, nullptr);
        p.setInput(QStringLiteral("It is OK, she said."));
        QVERIFY(runPanel(&p));
        QVERIFY2(p.resultText().contains(QStringLiteral("okay")), qPrintable(p.resultText()));
        QVERIFY(p.notes().contains(QStringLiteral("Your rule")));
        m_host->settings.remove(QStringLiteral("translation-proofreading/replacements"));
    }

    void formalStyle()
    {
        if (!haveModels())
            QSKIP("TP_MODELS_DIR not set");
        Panel p(m_tool, Panel::Place::Inspector, nullptr);
        p.findChild<QComboBox *>(QStringLiteral("tpStyle"))->setCurrentIndex(1);
        p.setInput(QStringLiteral("I dont think he is coming, she said, more to herself then to anyone else."));
        QVERIFY(runPanel(&p));
        qInfo("formal: %s", qPrintable(p.resultText()));
        QVERIFY(p.resultText().contains(QStringLiteral("do not")));
    }

    void window()
    {
        m_tool->run(TranslateTool::kWindow);
        QWidget *w = nullptr;
        for (QWidget *top : QApplication::topLevelWidgets())
            if (top->objectName() == QLatin1String("tpWindow"))
                w = top;
        QVERIFY(w && w->isVisible());
        QCOMPARE(m_tool->openWindows(), 1);
        QTRY_VERIFY(m_host->states.value(TranslateTool::kWindow).first);
        auto *p = w->findChild<Panel *>();
        QVERIFY(p);
        w->resize(1100, 520);
        if (haveModels()) {
            p->setInput(kEnglish);
            QVERIFY(runPanel(p));
        }
        QCoreApplication::processEvents();
        shot(w, QStringLiteral("window"));
        // the inspector command asks the program for the tab
        m_tool->run(TranslateTool::kInspector);
        QCOMPARE(m_host->shownTabs.value(0), TranslateTool::kTab);
        w->close();
        QCOMPARE(m_tool->openWindows(), 0);
    }

    void speed()
    {
        if (!haveModels())
            QSKIP("TP_MODELS_DIR not set");
        // about 1,000 words, to tell the writer what to expect
        QStringList pars;
        while (words(pars.join(QLatin1Char('\n'))).size() < 1000)
            pars << kEnglish;
        Panel p(m_tool, Panel::Place::Inspector, nullptr);
        p.setInput(pars.join(QLatin1Char('\n')));
        QElapsedTimer t;
        t.start();
        QVERIFY(runPanel(&p, 900000));
        const double s = t.elapsed() / 1000.0;
        const int n = words(p.inputText()).size();
        qInfo("speed: %d words proofread in %.1f s (%.0f words/s, %d threads)", n, s, n / s, m_tool->threads());
    }
};

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    g_shots = qEnvironmentVariable("TP_SHOTS", QStringLiteral(TP_BUILD_DIR "/screenshots"));
    UiTests t;
    return QTest::qExec(&t, argc, argv);
}

#include "ui_tests.moc"
