// Checks of the text work without models: language, sentences, rules, safety checks, the word comparison; 1.1: dialogue
// kept as written, spelling suggestions, similar words, single words, and the chat models' tokenizer against the
// pieces of the model's own tokenizer (tokenizer-check.json of each chat model in TP_MODELS_DIR).
#include "Bpe.h"
#include "Logic.h"

#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <QtTest>
#include <QFileInfo>

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

    // 1.1: what the model did to dialogue in the writer's test of 2026-11-15 (our own sentences of the same kind)
    void dialogue()
    {
        // a quotation mark turned, one moved before the question mark, a closing one dropped, a lone one added
        QCOMPARE(keepDialogue(QStringLiteral("“Who’s ‘hmm’?”"), QStringLiteral("\"Who's 'hmm?'?")),
                 QStringLiteral("“Who’s ‘hmm’?”"));
        QCOMPARE(keepDialogue(QStringLiteral("“Fine. I’m tired.”"), QStringLiteral("\"Fine. I'm tired. \"")),
                 QStringLiteral("“Fine. I’m tired.”"));
        QCOMPARE(keepDialogue(QStringLiteral("How do I talk to a river?”"), QStringLiteral("How do I talk to a river?")),
                 QStringLiteral("How do I talk to a river?”"));
        // a real correction inside dialogue stays, with the writer's quotation marks around it
        QCOMPARE(keepDialogue(QStringLiteral("“Well that’s easy.”"), QStringLiteral("\"Well, that's easy.\"")),
                 QStringLiteral("“Well, that’s easy.”"));
        QCOMPARE(keepDialogue(QStringLiteral("“Then if would help.”"), QStringLiteral("\"Then it would help.\"")),
                 QStringLiteral("“Then it would help.”"));
        // a lone comma goes to the word before it
        QCOMPARE(keepDialogue(QStringLiteral("if it could even talk"), QStringLiteral("if it could even , talk")),
                 QStringLiteral("if it could even, talk"));
        // quotation marks the model added go
        QCOMPARE(keepDialogue(QStringLiteral("He said no."), QStringLiteral("He said \"no.\"")), QStringLiteral("He said no."));
        // a word left out: its quotation mark goes to its neighbor
        QCOMPARE(keepDialogue(QStringLiteral("“Very very good.”"), QStringLiteral("\"Very good.\"")),
                 QStringLiteral("“Very good.”"));
        QCOMPARE(tidyPunctuation(QStringLiteral("Hello , world ( yes ) !")), QStringLiteral("Hello, world (yes)!"));
    }

    void spelling()
    {
        QVERIFY(knownWord(QStringLiteral("beautiful")));
        QVERIFY(knownWord(QStringLiteral("Don’t")));
        QVERIFY(!knownWord(QStringLiteral("beautifull")));
        const QStringList s = spellingSuggestions(QStringLiteral("beautifull"));
        QVERIFY2(!s.isEmpty() && s.first() == QLatin1String("beautiful"), qPrintable(s.join(QLatin1Char(','))));
        const QStringList t = spellingSuggestions(QStringLiteral("Recieve"));
        QVERIFY2(t.contains(QStringLiteral("Receive")), qPrintable(t.join(QLatin1Char(','))));
        QVERIFY(spellingSuggestions(QStringLiteral("loose")).contains(QStringLiteral("lose")));
        QCOMPARE(spellingSuggestions(QStringLiteral("dont")).value(0), QStringLiteral("don't"));
        QVERIFY(knownWord(QStringLiteral("river’s")));
        QVERIFY(knownWord(QStringLiteral("isn't")));
    }

    void similar()
    {
        QCOMPARE(baseForms(QStringLiteral("went")).value(0), QStringLiteral("go"));
        QCOMPARE(baseForms(QStringLiteral("cities")).value(0), QStringLiteral("city"));
        QVERIFY(baseForms(QStringLiteral("stopped")).contains(QStringLiteral("stop")));
        QString base;
        const QList<Similar> b = similarWords(QStringLiteral("beautiful"), &base);
        QCOMPARE(base, QStringLiteral("beautiful"));
        QVERIFY(!b.isEmpty() && b.first().pos == QLatin1String("adjective"));
        QVERIFY2(b.first().words.contains(QStringLiteral("handsome")), qPrintable(b.first().words.join(QLatin1Char(','))));
        // American spelling: WordNet's "well-favoured" becomes "well-favored"
        QVERIFY(!b.first().words.contains(QStringLiteral("well-favoured")));
        QVERIFY(b.first().words.contains(QStringLiteral("well-favored")));
        const QList<Similar> w = similarWords(QStringLiteral("walked"), &base);
        QCOMPARE(base, QStringLiteral("walk"));
        bool verb = false;
        for (const Similar &x : w)
            verb = verb || x.pos == QLatin1String("verb");
        QVERIFY(verb);
        QVERIFY(similarWords(QStringLiteral("xqzt")).isEmpty());
    }

    void modelWords()
    {
        // the information area: no repeats in another case, not the word itself; proofreading: no other form of it
        const QStringList found{QStringLiteral("windows,"), QStringLiteral("Windows"), QStringLiteral("door"),
                                QStringLiteral("“Window”"), QStringLiteral("wind"), QStringLiteral("door.")};
        QCOMPARE(usefulModelWords(found, QStringLiteral("window"), false), (QStringList{QStringLiteral("door"), QStringLiteral("wind")}));
        QCOMPARE(usefulModelWords(found, QStringLiteral("window")),
                 (QStringList{QStringLiteral("windows"), QStringLiteral("door"), QStringLiteral("wind")}));
        // a word alone (2026-11-15: "beautifull" gave "It, This, The, I, My"): only words close in spelling or meaning
        const QStringList near{QStringLiteral("beautifully"), QStringLiteral("fair"), QStringLiteral("beautifull")};
        QCOMPARE(usefulModelWords({QStringLiteral("It"), QStringLiteral("Beautifully"), QStringLiteral("This"),
                                   QStringLiteral("Fair"), QStringLiteral("beautful"), QStringLiteral("My")},
                                  QStringLiteral("beautiful"), true, &near),
                 (QStringList{QStringLiteral("Beautifully"), QStringLiteral("Fair"), QStringLiteral("beautful")}));
    }

    void singleWord()
    {
        QString w;
        QVERIFY(isSingleWord(QStringLiteral("  beautifull  "), &w));
        QCOMPARE(w, QStringLiteral("beautifull"));
        QVERIFY(isSingleWord(QStringLiteral("“don’t”"), &w));
        QCOMPARE(w, QStringLiteral("don’t"));
        QVERIFY(isSingleWord(QStringLiteral("well-known."), &w));
        QVERIFY(!isSingleWord(QStringLiteral("two words")));
        QVERIFY(!isSingleWord(QStringLiteral("42")));
        QVERIFY(!isSingleWord(QStringLiteral("...")));
    }

    // the chat models' tokenizer gives the pieces the model's own tokenizer gave (models-2: tokenizer-check.json)
    void bpeTokenizer()
    {
        // without a model: the byte map and the word starts
        QCOMPARE(BpeTokenizer::byteLevel(QByteArray(" ")), std::string("\xc4\xa0"));
        QCOMPARE(BpeTokenizer::byteLevel(QStringLiteral("—").toUtf8()), std::string("\xc3\xa2\xc4\xa2\xc4\xb6"));
        QVERIFY(BpeTokenizer::startsWord("\xc4\xa0word"));
        QVERIFY(!BpeTokenizer::startsWord("word"));
        const QString models = qEnvironmentVariable("TP_MODELS_DIR");
        if (models.isEmpty())
            QSKIP("TP_MODELS_DIR not set");
        int checked = 0;
        for (const QString &id : {QStringLiteral("qwen3-1.7b"), QStringLiteral("qwen3-4b")}) {
            QString tokJson = models + QStringLiteral("/%1--tokenizer.json").arg(id);
            QString checkJson = models + QStringLiteral("/%1--tokenizer-check.json").arg(id);
            if (!QFileInfo::exists(tokJson)) {
                tokJson = models + QStringLiteral("/%1/tokenizer.json").arg(id);
                checkJson = models + QStringLiteral("/%1/tokenizer-check.json").arg(id);
            }
            if (!QFileInfo::exists(tokJson))
                continue;
            BpeTokenizer t;
            QString error;
            QVERIFY2(t.load(tokJson, &error), qPrintable(error));
            QFile f(checkJson);
            QVERIFY(f.open(QIODevice::ReadOnly));
            for (const QJsonValue &v : QJsonDocument::fromJson(f.readAll()).object().value(QStringLiteral("samples")).toArray()) {
                const QString text = v.toObject().value(QStringLiteral("text")).toString();
                std::vector<std::string> want;
                for (const QJsonValue &p : v.toObject().value(QStringLiteral("tokens")).toArray())
                    want.push_back(p.toString().toStdString());
                const std::vector<std::string> got = t.encode(text);
                if (got != want) {
                    QStringList g, w;
                    for (const auto &x : got)
                        g << QString::fromStdString(x);
                    for (const auto &x : want)
                        w << QString::fromStdString(x);
                    QFAIL(qPrintable(QStringLiteral("%1: %2\n  ours:  %3\n  model: %4").arg(id, text, g.join(QLatin1Char('|')),
                                                                                           w.join(QLatin1Char('|')))));
                }
                // and back: the text again (special tokens left out)
                QString back = t.decode(got);
                if (!text.contains(QLatin1String("<|")))
                    QCOMPARE(back, text);
                ++checked;
            }
        }
        if (!checked)
            QSKIP("no chat model's tokenizer in TP_MODELS_DIR");
    }
};

QTEST_GUILESS_MAIN(LogicTests)
#include "logic_tests.moc"
