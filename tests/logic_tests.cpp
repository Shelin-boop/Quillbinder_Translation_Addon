// Checks of the text work without models: language, sentences, rules, safety checks, the word comparison.
#include "Logic.h"

#include <QtTest>

using namespace tp;

class LogicTests : public QObject
{
    Q_OBJECT
private slots:
    void language()
    {
        QCOMPARE(detectLanguage(QStringLiteral("The rain had not stopped for three days, and she was tired.")), QStringLiteral("en"));
        QCOMPARE(detectLanguage(QStringLiteral("Der Regen hatte seit drei Tagen nicht aufgehört.")), QStringLiteral("de"));
        QCOMPARE(detectLanguage(QStringLiteral("„Ich glaube nicht, dass er kommt“, sagte sie leise.")), QStringLiteral("de"));
        QCOMPARE(detectLanguage(QStringLiteral("He said: \"Danke schön\" and left the room with the others.")), QStringLiteral("en"));
    }

    void sentences()
    {
        const QStringList s = splitSentences(QStringLiteral("Mr. Smith came home. “Hello,” he said. It was late! Was it?"));
        QCOMPARE(s, (QStringList{QStringLiteral("Mr. Smith came home."), QStringLiteral("“Hello,” he said."),
                                 QStringLiteral("It was late!"), QStringLiteral("Was it?")}));
        QCOMPARE(splitSentences(QStringLiteral("“Come on.” She left.")),
                 (QStringList{QStringLiteral("“Come on.”"), QStringLiteral("She left.")}));
        QCOMPARE(splitSentences(QStringLiteral("J. R. Miller wrote it. Done")).size(), 2);
        QCOMPARE(splitSentences(QStringLiteral("no end mark")), QStringList{QStringLiteral("no end mark")});
    }

    void plan()
    {
        const QString text = QStringLiteral("One. Two.\n\nThree four. Five.");
        const Plan p = makePlan(text, false);
        QCOMPARE(p.units.size(), 4);
        QCOMPARE(p.paragraphs.size(), 3);
        QVERIFY(p.paragraphs.at(1).isEmpty());
        QCOMPARE(joinPlan(p, p.units), text);
        const Plan g = makePlan(text, true);
        QCOMPARE(g.units, (QStringList{QStringLiteral("One. Two."), QStringLiteral("Three four. Five.")}));
        QCOMPARE(joinPlan(g, g.units), text);
    }

    void quotes()
    {
        QCOMPARE(toCurlyQuotes(QStringLiteral("\"I don't know,\" she said. 'Fine.'")),
                 QStringLiteral("“I don’t know,” she said. ‘Fine.’"));
        QCOMPARE(toPlainQuotes(QStringLiteral("„Ja“, ’s")), QStringLiteral("\"Ja\", 's"));
    }

    void dashes()
    {
        Rules r;
        int n = 0;
        QCOMPARE(beforeModel(QStringLiteral("ticked on — slow and indifferent"), r, &n), QStringLiteral("ticked on, slow and indifferent"));
        QCOMPARE(n, 1);
        QCOMPARE(beforeModel(QStringLiteral("on—slow"), r, &n), QStringLiteral("on, slow"));
        QCOMPARE(beforeModel(QStringLiteral("\"I was going—\" she said"), r, &n), QStringLiteral("\"I was going...\" she said"));
        QCOMPARE(beforeModel(QStringLiteral("He waited -- then left."), r, &n), QStringLiteral("He waited, then left."));
        // a range and a hyphenated word stay
        QCOMPARE(beforeModel(QStringLiteral("1990–2000, well-known"), r, &n), QStringLiteral("1990–2000, well-known"));
        QCOMPARE(n, 0);
        r.noEmDash = false;
        QCOMPARE(beforeModel(QStringLiteral("on — slow"), r, &n), QStringLiteral("on — slow"));
    }

    void afterModelRules()
    {
        Rules r;
        r.replacements = parseReplacements(QStringLiteral("# a note\nOK => okay\nalright => all right\nbroken line"));
        QCOMPARE(r.replacements.size(), 2);
        QStringList notes;
        const QString out = afterModel(QStringLiteral("The grey Colour of the theatre... \"OK,\" she said, alright. Towards it."), r, &notes);
        QCOMPARE(out, QStringLiteral("The gray Color of the theater... “okay,” she said, all right. Toward it."));
        QVERIFY(notes.contains(QStringLiteral("American spelling: grey → gray")));
        QVERIFY(notes.contains(QStringLiteral("Your rule: OK → okay")));
        QCOMPARE(afterModel(QStringLiteral("Wait…"), r), QStringLiteral("Wait..."));
        r.american = false;
        r.curlyQuotes = false;
        QCOMPARE(afterModel(QStringLiteral("grey \"x\""), r), QStringLiteral("grey \"x\""));
        QStringList errors;
        parseReplacements(QStringLiteral("no arrow here\n=> nothing"), &errors);
        QCOMPARE(errors.size(), 2);
    }

    void americanList()
    {
        const QHash<QString, QString> &l = tp::spellingList();
        QVERIFY(l.size() > 2500);
        QCOMPARE(l.value(QStringLiteral("realise")), QStringLiteral("realize"));
        QCOMPARE(l.value(QStringLiteral("travelled")), QStringLiteral("traveled"));
        QVERIFY(!l.contains(QStringLiteral("prise")));     // pry, not prize
        QVERIFY(!l.contains(QStringLiteral("dialogue")));  // American writing keeps it
    }

    void safety()
    {
        QVERIFY(similarity(QStringLiteral("abc"), QStringLiteral("abc")) > 0.99);
        QVERIFY(changedTooMuch(QStringLiteral("Night."), QStringLiteral("The night.")));
        QVERIFY(!changedTooMuch(QStringLiteral("Mara was starting to loose her patience."),
                                QStringLiteral("Mara was starting to lose her patience.")));
        QVERIFY(newDoubledWord(QStringLiteral("then thought better of it"), QStringLiteral("then then I thought better of it")));
        QVERIFY(!newDoubledWord(QStringLiteral("it had had an effect"), QStringLiteral("it had had an effect.")));
    }

    void compare()
    {
        QList<Segment> s = compareWords(QStringLiteral("people was hurrying under there umbrellas"),
                                        QStringLiteral("people were hurrying under their umbrellas"));
        int changes = 0;
        for (const Segment &x : s)
            changes += x.changed;
        QCOMPARE(changes, 2);
        QCOMPARE(segmentsText(s), QStringLiteral("people were hurrying under their umbrellas"));
        for (Segment &x : s)
            x.accepted = false;
        QCOMPARE(segmentsText(s), QStringLiteral("people was hurrying under there umbrellas"));
        // added and left-out words
        s = compareWords(QStringLiteral("she sat down and read"), QStringLiteral("she sat down, and read the book"));
        QCOMPARE(segmentsText(s), QStringLiteral("she sat down, and read the book"));
        s = compareWords(QStringLiteral("a b c"), QStringLiteral("a c"));
        QCOMPARE(s.size(), 3);
        QVERIFY(s.at(1).changed && s.at(1).after.isEmpty());
    }
};

QTEST_GUILESS_MAIN(LogicTests)
#include "logic_tests.moc"
