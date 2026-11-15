#include "Logic.h"

#include <QFile>
#include <QRegularExpression>
#include <QTextStream>

#include <algorithm>
#include <vector>

namespace tp {

namespace {

const QSet<QString> &germanWords()
{
    static const QSet<QString> s = [] {
        QSet<QString> out;
        for (const QString &w : QStringLiteral(
                 "der die das und ist nicht ein eine einen dem den des zu mit sich auf für von ich du er sie es wir "
                 "ihr war hatte haben wird werden auch noch nur aber wenn dass als wie im ins am beim vom zum zur "
                 "nach bei aus über unter schon sehr kein keine mein dein sein sind bin bist doch dann denn oder "
                 "weil hier jetzt immer wieder nichts etwas mir mich dir dich uns euch ihm ihn ihnen")
                                    .split(QLatin1Char(' ')))
            out.insert(w);
        return out;
    }();
    return s;
}

const QSet<QString> &englishWords()
{
    static const QSet<QString> s = [] {
        QSet<QString> out;
        for (const QString &w : QStringLiteral(
                 "the and is not a an of to in it that was he she they we you i for on with as at by this had "
                 "have has be been are were his her their there what which when would could from but or if then "
                 "than so very no my your our him them me us do did does just all one about into out up")
                                    .split(QLatin1Char(' ')))
            out.insert(w);
        return out;
    }();
    return s;
}

// Abbreviations after which a period does not end a sentence (English and German, lower case, without the period).
const QSet<QString> &abbreviations()
{
    static const QSet<QString> s = [] {
        QSet<QString> out;
        for (const QString &w : QStringLiteral("mr mrs ms dr st prof sr jr vs etc e.g i.e no nr ca bzw z.b d.h "
                                               "u.a usw vgl hr fr dipl ing st. mt ft lt col gen capt sgt rev")
                                    .split(QLatin1Char(' ')))
            out.insert(w);
        return out;
    }();
    return s;
}

QString keepCase(const QString &found, const QString &replacement)
{
    if (replacement.isEmpty() || found.isEmpty())
        return replacement;
    if (found.size() > 1 && found == found.toUpper() && found != found.toLower())
        return replacement.toUpper();
    if (found.at(0).isUpper() && replacement.at(0).isLower())
        return replacement.at(0).toUpper() + replacement.mid(1);
    return replacement;
}

QHash<QString, QString> g_spelling;
bool g_spellingLoaded = false;

} // namespace

QString detectLanguage(const QString &text)
{
    const QString lower = text.toLower();
    static const QRegularExpression wordRe(QStringLiteral("[a-zäöüß]+"));
    int de = 0, en = 0;
    for (auto it = wordRe.globalMatch(lower); it.hasNext();) {
        const QString w = it.next().captured(0);
        if (germanWords().contains(w))
            ++de;
        if (englishWords().contains(w))
            ++en;
    }
    for (QChar c : lower)
        if (c == QChar(0xe4) || c == QChar(0xf6) || c == QChar(0xfc) || c == QChar(0xdf))
            de += 3;
    return de > en ? QStringLiteral("de") : QStringLiteral("en");
}

QStringList splitSentences(const QString &paragraph)
{
    // an end mark (and closing quotes) followed by space and a capital, a digit or an opening quote
    static const QRegularExpression endRe(QStringLiteral("[.!?\u2026]+[\"\u201d\u2019\u00bb)]*(?=\\s+[\"\u201c\u201e\u2018\u00ab(]?[\\p{Lu}0-9])"),
                                          QRegularExpression::UseUnicodePropertiesOption);
    static const QRegularExpression lastWordRe(QStringLiteral("([\\w.]+)$"), QRegularExpression::UseUnicodePropertiesOption);
    QStringList out;
    int start = 0;
    for (auto it = endRe.globalMatch(paragraph); it.hasNext();) {
        const QRegularExpressionMatch m = it.next();
        if (m.captured(0).startsWith(QLatin1Char('.')) && m.captured(0).size() == 1) {
            const QString before = paragraph.mid(start, m.capturedStart() - start);
            const QRegularExpressionMatch w = lastWordRe.match(before);
            if (w.hasMatch() && (abbreviations().contains(w.captured(1).toLower())
                                 || (w.captured(1).size() == 1 && w.captured(1).at(0).isUpper())))
                continue; // "Mr. Smith", "J. Smith"
        }
        const QString s = paragraph.mid(start, m.capturedEnd() - start).trimmed();
        if (!s.isEmpty())
            out << s;
        start = m.capturedEnd();
    }
    const QString rest = paragraph.mid(start).trimmed();
    if (!rest.isEmpty())
        out << rest;
    return out;
}

Plan makePlan(const QString &text, bool groupForContext, int maxWords)
{
    Plan p;
    const QStringList pars = text.split(QLatin1Char('\n'));
    for (const QString &par : pars) {
        QList<int> idx;
        if (par.trimmed().isEmpty()) {
            p.paragraphs << idx;
            p.emptyParagraphs << par;
            continue;
        }
        QStringList units = splitSentences(par);
        if (groupForContext) {
            QStringList grouped;
            QString cur;
            for (const QString &s : units) {
                const QString joined = cur.isEmpty() ? s : cur + QLatin1Char(' ') + s;
                if (!cur.isEmpty() && joined.split(QLatin1Char(' '), Qt::SkipEmptyParts).size() > maxWords) {
                    grouped << cur;
                    cur = s;
                } else {
                    cur = joined;
                }
            }
            if (!cur.isEmpty())
                grouped << cur;
            units = grouped;
        }
        for (const QString &u : units) {
            idx << p.units.size();
            p.units << u;
        }
        p.paragraphs << idx;
        p.emptyParagraphs << QString();
    }
    return p;
}

QString joinPlan(const Plan &plan, const QStringList &unitTexts)
{
    QStringList pars;
    for (int i = 0; i < plan.paragraphs.size(); ++i) {
        const QList<int> &idx = plan.paragraphs.at(i);
        if (idx.isEmpty()) {
            pars << plan.emptyParagraphs.value(i);
            continue;
        }
        QStringList parts;
        for (int u : idx)
            parts << unitTexts.value(u);
        pars << parts.join(QLatin1Char(' '));
    }
    return pars.join(QLatin1Char('\n'));
}

QString toPlainQuotes(const QString &text)
{
    QString s = text;
    for (QChar c : {QChar(0x201e), QChar(0x201c), QChar(0x201d), QChar(0x00ab), QChar(0x00bb)})
        s.replace(c, QLatin1Char('"'));
    for (QChar c : {QChar(0x201a), QChar(0x2018), QChar(0x2019)})
        s.replace(c, QLatin1Char('\''));
    return s;
}

QString toCurlyQuotes(const QString &text)
{
    static const QRegularExpression openDouble(QStringLiteral("(^|[\\s(\\[{\u2014\u2013-])\""),
                                               QRegularExpression::UseUnicodePropertiesOption);
    static const QRegularExpression apostrophe(QStringLiteral("(\\w)'(\\w)"), QRegularExpression::UseUnicodePropertiesOption);
    static const QRegularExpression openSingle(QStringLiteral("(^|[\\s(\\[{\u201c])'"), QRegularExpression::UseUnicodePropertiesOption);
    QString s = text;
    s.replace(openDouble, QStringLiteral("\\1\u201c"));
    s.replace(QLatin1Char('"'), QChar(0x201d));
    s.replace(apostrophe, QStringLiteral("\\1\u2019\\2"));
    s.replace(openSingle, QStringLiteral("\\1\u2018"));
    s.replace(QLatin1Char('\''), QChar(0x2019));
    return s;
}

QList<QPair<QString, QString>> parseReplacements(const QString &text, QStringList *errors)
{
    QList<QPair<QString, QString>> out;
    const QStringList lines = text.split(QLatin1Char('\n'));
    for (int i = 0; i < lines.size(); ++i) {
        const QString line = lines.at(i).trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
            continue;
        const int arrow = line.indexOf(QLatin1String("=>"));
        const QString from = arrow < 0 ? QString() : line.left(arrow).trimmed();
        if (arrow < 0 || from.isEmpty()) {
            if (errors)
                errors->append(QStringLiteral("line %1: \"old => new\" expected").arg(i + 1));
            continue;
        }
        out.append({from, line.mid(arrow + 2).trimmed()});
    }
    return out;
}

QString beforeModel(const QString &text, const Rules &rules, int *dashes)
{
    if (dashes)
        *dashes = 0;
    if (!rules.noEmDash)
        return text;
    QString s = text;
    int n = 0;
    auto replaceAll = [&](const QRegularExpression &re, const QString &with) {
        int count = 0;
        for (auto it = re.globalMatch(s); it.hasNext(); it.next())
            ++count;
        if (count) {
            s.replace(re, with);
            n += count;
        }
    };
    // an interruption at the end of speech or a paragraph: "going—" becomes "going..."
    static const QRegularExpression atEnd(QStringLiteral("\\s*(\u2014|\\s\u2013|--)\\s*(?=[\"\u201d\u2019]|$)"),
                                          QRegularExpression::MultilineOption);
    // right after an opening quote: left out
    static const QRegularExpression atStart(QStringLiteral("(?<=[\"\u201c\u201e])\\s*(\u2014|\u2013\\s|--)\\s*"));
    // between words: a comma
    static const QRegularExpression between(QStringLiteral("\\s*\u2014\\s*|\\s+\u2013\\s+|(?<=\\w)\\s*--\\s*(?=\\w)"),
                                            QRegularExpression::UseUnicodePropertiesOption);
    replaceAll(atEnd, QStringLiteral("..."));
    replaceAll(atStart, QString());
    replaceAll(between, QStringLiteral(", "));
    if (dashes)
        *dashes = n;
    return s;
}

void loadSpellingList(const QString &file)
{
    g_spelling.clear();
    QFile f(file);
    if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&f);
        while (!in.atEnd()) {
            const QString line = in.readLine();
            if (line.startsWith(QLatin1Char('#')))
                continue;
            const int tab = line.indexOf(QLatin1Char('\t'));
            if (tab > 0)
                g_spelling.insert(line.left(tab), line.mid(tab + 1).trimmed());
        }
    }
    g_spellingLoaded = true;
}

const QHash<QString, QString> &spellingList()
{
    if (!g_spellingLoaded)
        loadSpellingList(QStringLiteral(":/translation-proofreading/us-spelling.tsv"));
    return g_spelling;
}

QString afterModel(const QString &text, const Rules &rules, QStringList *notes)
{
    QString s = text;
    auto note = [notes](const QString &n) {
        if (notes && !notes->contains(n))
            notes->append(n);
    };
    if (rules.noEmDash && (s.contains(QChar(0x2014)) || s.contains(QLatin1String(" \u2013 ")))) {
        int n = 0;
        s = beforeModel(s, rules, &n);
    }
    if (rules.american) {
        static const QRegularExpression wordRe(QStringLiteral("[A-Za-z]+"));
        const QHash<QString, QString> &list = spellingList();
        QString out;
        int last = 0;
        for (auto it = wordRe.globalMatch(s); it.hasNext();) {
            const QRegularExpressionMatch m = it.next();
            out += s.mid(last, m.capturedStart() - last);
            const QString w = m.captured(0);
            const auto f = list.constFind(w.toLower());
            if (f != list.constEnd()) {
                const QString us = keepCase(w, f.value());
                out += us;
                note(QStringLiteral("American spelling: %1 \u2192 %2").arg(w, us));
            } else {
                out += w;
            }
            last = m.capturedEnd();
        }
        out += s.mid(last);
        s = out;
    }
    for (const auto &r : rules.replacements) {
        const QRegularExpression re(QStringLiteral("(?<!\\w)%1(?!\\w)").arg(QRegularExpression::escape(r.first)),
                                    QRegularExpression::CaseInsensitiveOption | QRegularExpression::UseUnicodePropertiesOption);
        QString out;
        int last = 0;
        bool any = false;
        for (auto it = re.globalMatch(s); it.hasNext();) {
            const QRegularExpressionMatch m = it.next();
            // written as in the rule: the replacement as written; else the case of the first letter is kept
            const QString found = m.captured(0);
            out += s.mid(last, m.capturedStart() - last) + (found == r.first ? r.second : keepCase(found, r.second));
            last = m.capturedEnd();
            any = true;
        }
        if (any) {
            out += s.mid(last);
            s = out;
            note(QStringLiteral("Your rule: %1 \u2192 %2").arg(r.first, r.second));
        }
    }
    if (rules.plainEllipsis)
        s.replace(QChar(0x2026), QLatin1String("..."));
    if (rules.curlyQuotes)
        s = toCurlyQuotes(toPlainQuotes(s));
    return s;
}

double similarity(const QString &a, const QString &b)
{
    if (a.isEmpty() && b.isEmpty())
        return 1.0;
    const int n = a.size(), m = b.size();
    std::vector<int> prev(m + 1, 0), cur(m + 1, 0);
    for (int i = 1; i <= n; ++i) {
        for (int j = 1; j <= m; ++j)
            cur[j] = a.at(i - 1) == b.at(j - 1) ? prev[j - 1] + 1 : std::max(prev[j], cur[j - 1]);
        std::swap(prev, cur);
    }
    return 2.0 * prev[m] / (n + m);
}

bool changedTooMuch(const QString &original, const QString &corrected, double minSimilarity)
{
    return similarity(original, corrected) < minSimilarity;
}

QSet<QString> doubledWords(const QString &text)
{
    static const QRegularExpression wordRe(QStringLiteral("[\\w'\u2019]+"), QRegularExpression::UseUnicodePropertiesOption);
    QStringList w;
    for (auto it = wordRe.globalMatch(text.toLower()); it.hasNext();)
        w << it.next().captured(0);
    QSet<QString> out;
    for (int i = 1; i < w.size(); ++i)
        if (w.at(i) == w.at(i - 1))
            out.insert(w.at(i));
    return out;
}

bool newDoubledWord(const QString &original, const QString &corrected)
{
    const QSet<QString> before = doubledWords(original);
    for (const QString &w : doubledWords(corrected))
        if (!before.contains(w))
            return true;
    return false;
}

QStringList words(const QString &text)
{
    return text.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
}

QList<Segment> compareWords(const QString &original, const QString &corrected)
{
    const QStringList a = words(original), b = words(corrected);
    const int n = a.size(), m = b.size();
    // longest common subsequence of words
    std::vector<std::vector<int>> L(n + 1, std::vector<int>(m + 1, 0));
    for (int i = n - 1; i >= 0; --i)
        for (int j = m - 1; j >= 0; --j)
            L[i][j] = a.at(i) == b.at(j) ? L[i + 1][j + 1] + 1 : std::max(L[i + 1][j], L[i][j + 1]);
    QList<Segment> out;
    auto addSame = [&](const QString &w) {
        if (!out.isEmpty() && !out.last().changed) {
            out.last().before << w;
            out.last().after << w;
        } else {
            Segment s;
            s.before << w;
            s.after << w;
            out << s;
        }
    };
    auto addChange = [&](const QString &before, const QString &after) {
        if (out.isEmpty() || !out.last().changed) {
            Segment s;
            s.changed = true;
            out << s;
        }
        if (!before.isNull())
            out.last().before << before;
        if (!after.isNull())
            out.last().after << after;
    };
    int i = 0, j = 0;
    while (i < n || j < m) {
        if (i < n && j < m && a.at(i) == b.at(j)) {
            addSame(a.at(i));
            ++i;
            ++j;
        } else if (j < m && (i == n || L[i][j + 1] >= L[i + 1][j])) {
            addChange(QString(), b.at(j));
            ++j;
        } else {
            addChange(a.at(i), QString());
            ++i;
        }
    }
    return out;
}

QString segmentsText(const QList<Segment> &segments)
{
    QStringList w;
    for (const Segment &s : segments)
        w << (s.changed && !s.accepted ? s.before : s.after);
    return w.join(QLatin1Char(' '));
}

} // namespace tp
