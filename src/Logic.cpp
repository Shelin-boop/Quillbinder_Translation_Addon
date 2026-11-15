#include "Logic.h"

#include <QFile>
#include <QRegularExpression>
#include <QTextStream>

#include <algorithm>
#include <cstdlib>
#include <tuple>
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

// ---------------------------------------------------------------- 1.1: dialogue kept as written

namespace {

bool isQuoteChar(QChar c)
{
    switch (c.unicode()) {
    case '"': case '\'': case 0x201c: case 0x201d: case 0x201e: case 0x00ab: case 0x00bb:
    case 0x2018: case 0x2019: case 0x201a: case 0x2039: case 0x203a:
        return true;
    default:
        return false;
    }
}

bool isTrailPunct(QChar c)
{
    return c == QLatin1Char('.') || c == QLatin1Char(',') || c == QLatin1Char(';') || c == QLatin1Char(':')
           || c == QLatin1Char('!') || c == QLatin1Char('?') || c == QChar(0x2026) || c == QLatin1Char(')');
}

// a word in three parts: quotation marks before it, the word, quotation marks and punctuation after it
struct Parts {
    QString lead, core, trail;
    bool leadQuote() const { return !lead.isEmpty(); }
    bool trailQuote() const
    {
        for (QChar c : trail)
            if (isQuoteChar(c))
                return true;
        return false;
    }
};

Parts parts(const QString &w)
{
    Parts p;
    int a = 0, b = int(w.size());
    while (a < b && isQuoteChar(w.at(a)))
        ++a;
    // an apostrophe that ends a word of the list ("sleepin'") would be a quotation mark here; the original keeps it
    while (b > a && (isQuoteChar(w.at(b - 1)) || isTrailPunct(w.at(b - 1))))
        --b;
    p.lead = w.left(a);
    p.core = w.mid(a, b - a);
    p.trail = w.mid(b);
    return p;
}

QString withoutQuotes(const QString &s)
{
    QString out;
    for (QChar c : s)
        if (!isQuoteChar(c))
            out += c;
    return out;
}

bool onlyQuotes(const QString &w)
{
    for (QChar c : w)
        if (!isQuoteChar(c))
            return false;
    return true;
}

bool onlyPunct(const QString &w)
{
    for (QChar c : w)
        if (!isTrailPunct(c) && !isQuoteChar(c))
            return false;
    return true;
}

QString key(const QString &core)
{
    QString k;
    for (QChar c : core.toLower())
        if (c.isLetterOrNumber())
            k += c;
    return k.isEmpty() ? core : k;
}

} // namespace

QString tidyPunctuation(const QString &text)
{
    static const QRegularExpression before(QStringLiteral("\\s+([,.;:!?\u2026)])"));
    static const QRegularExpression after(QStringLiteral("\\(\\s+"));
    QString s = text;
    s.replace(before, QStringLiteral("\\1"));
    s.replace(after, QStringLiteral("("));
    return s;
}

QString keepDialogue(const QString &original, const QString &corrected)
{
    const QStringList ow = words(original);
    // the model's words: lone quotation marks go, lone punctuation joins the word before
    QStringList cw;
    for (const QString &w : words(corrected)) {
        if (onlyQuotes(w))
            continue;
        if (onlyPunct(w) && !cw.isEmpty()) {
            cw.last() += withoutQuotes(w);
            continue;
        }
        cw << w;
    }
    QList<Parts> op, cp;
    for (const QString &w : ow)
        op << parts(w);
    for (const QString &w : cw)
        cp << parts(w);
    const int n = int(op.size()), m = int(cp.size());
    std::vector<std::vector<int>> L(size_t(n) + 1, std::vector<int>(size_t(m) + 1, 0));
    for (int i = n - 1; i >= 0; --i)
        for (int j = m - 1; j >= 0; --j)
            L[size_t(i)][size_t(j)] = key(op.at(i).core) == key(cp.at(j).core) ? L[size_t(i) + 1][size_t(j) + 1] + 1
                                      : std::max(L[size_t(i) + 1][size_t(j)], L[size_t(i)][size_t(j) + 1]);
    QStringList out;
    QString pendingLead;   // quotation marks of left-out words, for the next word
    auto rebuilt = [](const Parts &o, const Parts &c) {
        // the quotation marks where the writer had them; at a closing one the writer's punctuation too; the word as
        // written when only its apostrophe differs ("I'm" for "I’m")
        const QString core = toPlainQuotes(o.core) == toPlainQuotes(c.core) ? o.core : c.core;
        return o.lead + core + (o.trailQuote() ? o.trail : withoutQuotes(c.trail));
    };
    auto flushRegion = [&](int oi0, int oi1, int cj0, int cj1) {
        // a stretch the model changed: its words, with the quotation marks of the original's first and last word
        QString lead, trail;
        for (int i = oi0; i < oi1; ++i) {
            if (i == oi0)
                lead = op.at(i).lead;
            if (op.at(i).trailQuote())
                trail = op.at(i).trail;
        }
        if (cj1 == cj0) {   // words left out: their quotation marks go to the neighbors
            pendingLead += lead;
            if (!trail.isEmpty() && !out.isEmpty()) {
                const Parts last = parts(out.last());
                out.last() = last.lead + last.core + trail;
            }
            return;
        }
        for (int j = cj0; j < cj1; ++j) {
            const Parts &c = cp.at(j);
            QString w = withoutQuotes(c.lead) + c.core;
            const bool last = j == cj1 - 1;
            w += last && !trail.isEmpty() ? trail : withoutQuotes(c.trail);
            if (j == cj0)
                w = pendingLead + lead + w;
            out << w;
            pendingLead.clear();
        }
    };
    int i = 0, j = 0, ri = 0, rj = 0;
    while (i < n || j < m) {
        if (i < n && j < m && key(op.at(i).core) == key(cp.at(j).core)) {
            if (ri < i || rj < j)
                flushRegion(ri, i, rj, j);
            out << pendingLead + rebuilt(op.at(i), cp.at(j));
            pendingLead.clear();
            ++i;
            ++j;
            ri = i;
            rj = j;
        } else if (j < m && (i == n || L[size_t(i)][size_t(j) + 1] >= L[size_t(i) + 1][size_t(j)])) {
            ++j;
        } else {
            ++i;
        }
    }
    if (ri < n || rj < m)
        flushRegion(ri, n, rj, m);
    if (!pendingLead.isEmpty() && !out.isEmpty())
        out.last() += pendingLead;
    return out.join(QLatin1Char(' '));
}

// ---------------------------------------------------------------- 1.1: spelling suggestions, similar words

namespace {

QSet<QString> g_words;
bool g_wordsLoaded = false;

struct Thesaurus {
    QHash<QString, QString> entries;      // word -> "n:a;b|v:c"
    QHash<QString, QStringList> irregular; // "went" -> {"go"}
    bool loaded = false;
};
Thesaurus g_thesaurus;

const Thesaurus &thesaurus()
{
    if (!g_thesaurus.loaded)
        loadThesaurus(QStringLiteral(":/translation-proofreading/en-thesaurus.tsv"));
    return g_thesaurus;
}

// Damerau-Levenshtein distance (with swapped neighbors), stopped above `limit`
int distance(const QString &a, const QString &b, int limit)
{
    const int n = int(a.size()), m = int(b.size());
    if (std::abs(n - m) > limit)
        return limit + 1;
    std::vector<std::vector<int>> d(size_t(n) + 1, std::vector<int>(size_t(m) + 1, 0));
    for (int i = 0; i <= n; ++i)
        d[size_t(i)][0] = i;
    for (int j = 0; j <= m; ++j)
        d[0][size_t(j)] = j;
    for (int i = 1; i <= n; ++i) {
        int rowMin = limit + 1;
        for (int j = 1; j <= m; ++j) {
            const int cost = a.at(i - 1) == b.at(j - 1) ? 0 : 1;
            int v = std::min({d[size_t(i) - 1][size_t(j)] + 1, d[size_t(i)][size_t(j) - 1] + 1,
                              d[size_t(i) - 1][size_t(j) - 1] + cost});
            if (i > 1 && j > 1 && a.at(i - 1) == b.at(j - 2) && a.at(i - 2) == b.at(j - 1))
                v = std::min(v, d[size_t(i) - 2][size_t(j) - 2] + 1);
            d[size_t(i)][size_t(j)] = v;
            rowMin = std::min(rowMin, v);
        }
        if (rowMin > limit)
            return limit + 1;
    }
    return d[size_t(n)][size_t(m)];
}

QString posName(const QString &tag)
{
    if (tag == QLatin1String("n"))
        return QStringLiteral("noun");
    if (tag == QLatin1String("v"))
        return QStringLiteral("verb");
    if (tag == QLatin1String("a"))
        return QStringLiteral("adjective");
    return QStringLiteral("adverb");
}

} // namespace

void loadWordList(const QString &file)
{
    g_words.clear();
    QFile f(file);
    if (f.open(QIODevice::ReadOnly))
        for (const QByteArray &line : f.readAll().split('\n'))
            if (!line.isEmpty())
                g_words.insert(QString::fromUtf8(line));
    g_wordsLoaded = true;
}

const QSet<QString> &wordList()
{
    if (!g_wordsLoaded)
        loadWordList(QStringLiteral(":/translation-proofreading/en-words.txt"));
    return g_words;
}

namespace {

// the word list has no apostrophes: the common contractions, as candidates of the spelling suggestions too
const QStringList &contractions()
{
    static const QStringList c = QStringLiteral(
        "don't doesn't didn't isn't aren't wasn't weren't can't couldn't won't wouldn't shouldn't haven't hasn't "
        "hadn't mustn't needn't I'm I've I'll I'd you're you've you'll you'd we're we've we'll we'd they're they've "
        "they'll they'd it's it'll that's there's here's what's who's where's let's he's she's he'll she'll he'd "
        "she'd o'clock ma'am y'all ain't")
                                     .split(QLatin1Char(' '));
    return c;
}

bool inList(const QString &w)
{
    const QSet<QString> &list = wordList();
    return list.contains(w) || list.contains(w.toLower())
           || (w.size() > 1 && list.contains(w.at(0).toUpper() + w.mid(1).toLower()));
}

} // namespace

bool knownWord(const QString &word)
{
    QString w = word;
    w.replace(QChar(0x2019), QLatin1Char('\''));
    if (inList(w))
        return true;
    if (!w.contains(QLatin1Char('\'')))
        return false;
    for (const QString &c : contractions())
        if (c.compare(w, Qt::CaseInsensitive) == 0)
            return true;
    // "the river's", "Mara'll": the word before the ending
    for (const char *end : {"n't", "'s", "'re", "'ve", "'ll", "'d", "'m", "'"})
        if (w.endsWith(QLatin1String(end), Qt::CaseInsensitive) && w.size() > int(qstrlen(end)))
            return inList(w.left(w.size() - int(qstrlen(end))));
    return false;
}

QStringList spellingSuggestions(const QString &word, int max)
{
    QString w = word;
    w.replace(QChar(0x2019), QLatin1Char('\''));
    const QString lower = w.toLower();
    const bool capital = !w.isEmpty() && w.at(0).isUpper();
    struct Cand {
        QString word;
        int dist, common, firstSame, lenDiff;
    };
    std::vector<Cand> found;
    const Thesaurus &th = thesaurus();
    QStringList pool = contractions();
    for (const QString &c : std::as_const(pool)) {
        const QString cl = c.toLower();
        // only the apostrophe missing ("dont"): the nearest of all
        const int d = QString(cl).remove(QLatin1Char('\'')) == lower ? 0 : distance(lower, cl, 2);
        if (d <= 2 && cl != lower)
            found.push_back({c, d, 0, cl.at(0) == lower.at(0) ? 0 : 1, std::abs(int(c.size()) - int(lower.size()))});
    }
    for (const QString &c : wordList()) {
        if (std::abs(int(c.size()) - int(lower.size())) > 2)
            continue;
        const QString cl = c.toLower();
        if (cl == lower)
            continue;
        const int d = distance(lower, cl, 2);
        if (d > 2)
            continue;
        // a proper name (capital in the list) only for a word written with a capital
        if (c.at(0).isUpper() && !capital)
            continue;
        found.push_back({c, d, th.entries.contains(cl) ? 0 : 1, cl.at(0) == lower.at(0) ? 0 : 1,
                         std::abs(int(c.size()) - int(lower.size()))});
    }
    std::sort(found.begin(), found.end(), [](const Cand &a, const Cand &b) {
        return std::tie(a.dist, a.common, a.firstSame, a.lenDiff, a.word)
               < std::tie(b.dist, b.common, b.firstSame, b.lenDiff, b.word);
    });
    QStringList out;
    for (const Cand &c : found) {
        QString s = c.word;
        if (capital && s.at(0).isLower())
            s = s.at(0).toUpper() + s.mid(1);
        if (word.contains(QChar(0x2019)))
            s.replace(QLatin1Char('\''), QChar(0x2019));
        if (!out.contains(s))
            out << s;
        if (out.size() >= max)
            break;
    }
    return out;
}

void loadThesaurus(const QString &file)
{
    g_thesaurus = Thesaurus();
    QFile f(file);
    if (f.open(QIODevice::ReadOnly)) {
        for (const QByteArray &line : f.readAll().split('\n')) {
            if (line.isEmpty() || line.startsWith('#'))
                continue;
            const int tab = line.indexOf('\t');
            if (tab <= 0)
                continue;
            const QString k = QString::fromUtf8(line.left(tab));
            const QString v = QString::fromUtf8(line.mid(tab + 1));
            if (k.startsWith(QLatin1Char('@')))
                g_thesaurus.irregular.insert(k.mid(1), v.split(QLatin1Char(' '), Qt::SkipEmptyParts));
            else
                g_thesaurus.entries.insert(k, v);
        }
    }
    g_thesaurus.loaded = true;
}

QStringList baseForms(const QString &word)
{
    const Thesaurus &th = thesaurus();
    QString w = word.toLower();
    w.replace(QChar(0x2019), QLatin1Char('\''));
    QStringList out;
    auto add = [&](const QString &b) {
        if (th.entries.contains(b) && !out.contains(b))
            out << b;
    };
    add(w);
    for (const QString &b : th.irregular.value(w))
        add(b);
    // WordNet's rules for the regular forms (its "morphy"), and a doubled letter before -ed and -ing ("stopped")
    static const QList<QPair<QString, QString>> rules = {
        {QStringLiteral("s"), QString()},          {QStringLiteral("ses"), QStringLiteral("s")},
        {QStringLiteral("xes"), QStringLiteral("x")}, {QStringLiteral("zes"), QStringLiteral("z")},
        {QStringLiteral("ches"), QStringLiteral("ch")}, {QStringLiteral("shes"), QStringLiteral("sh")},
        {QStringLiteral("men"), QStringLiteral("man")}, {QStringLiteral("ies"), QStringLiteral("y")},
        {QStringLiteral("es"), QStringLiteral("e")}, {QStringLiteral("es"), QString()},
        {QStringLiteral("ed"), QStringLiteral("e")}, {QStringLiteral("ed"), QString()},
        {QStringLiteral("ing"), QStringLiteral("e")}, {QStringLiteral("ing"), QString()},
        {QStringLiteral("er"), QString()},         {QStringLiteral("est"), QString()},
        {QStringLiteral("er"), QStringLiteral("e")}, {QStringLiteral("est"), QStringLiteral("e")},
        {QStringLiteral("ied"), QStringLiteral("y")}, {QStringLiteral("ier"), QStringLiteral("y")},
        {QStringLiteral("iest"), QStringLiteral("y")}, {QStringLiteral("ly"), QString()},
    };
    for (const auto &r : rules) {
        if (w.size() > r.first.size() + 1 && w.endsWith(r.first)) {
            const QString stem = w.left(w.size() - r.first.size());
            add(stem + r.second);
            if ((r.first == QLatin1String("ed") || r.first == QLatin1String("ing")) && r.second.isEmpty()
                && stem.size() > 2 && stem.at(stem.size() - 1) == stem.at(stem.size() - 2))
                add(stem.left(stem.size() - 1));
        }
    }
    return out;
}

QList<Similar> similarWords(const QString &word, QString *base)
{
    QList<Similar> out;
    const QStringList bases = baseForms(word);
    if (base)
        *base = bases.value(0);
    const QHash<QString, QString> &us = spellingList();
    QSet<QString> seen;
    for (const QString &b : bases) {
        for (const QString &group : thesaurus().entries.value(b).split(QLatin1Char('|'), Qt::SkipEmptyParts)) {
            const int colon = group.indexOf(QLatin1Char(':'));
            if (colon < 0)
                continue;
            Similar s;
            s.pos = posName(group.left(colon));
            for (QString w : group.mid(colon + 1).split(QLatin1Char(';'), Qt::SkipEmptyParts)) {
                // American spelling: "well-favoured" becomes "well-favored"
                QStringList pieces = w.split(QLatin1Char('-'));
                for (QString &p : pieces)
                    p = us.value(p.toLower(), p);
                w = pieces.join(QLatin1Char('-'));
                const QString k = s.pos + QLatin1Char(':') + w.toLower();
                if (seen.contains(k) || w.compare(word, Qt::CaseInsensitive) == 0)
                    continue;
                // names ("Saul Bellow" for "bellow") only for a word written with a capital
                if (!word.isEmpty() && word.at(0).isLower() && !w.isEmpty() && w.at(0).isUpper())
                    continue;
                seen.insert(k);
                s.words << w;
            }
            if (s.words.isEmpty())
                continue;
            bool merged = false;
            for (Similar &o : out)
                if (o.pos == s.pos) {
                    o.words << s.words;
                    merged = true;
                }
            if (!merged)
                out << s;
        }
    }
    return out;
}

bool isSingleWord(const QString &text, QString *word)
{
    const QString t = text.trimmed();
    if (t.isEmpty() || t.contains(QRegularExpression(QStringLiteral("\\s"))))
        return false;
    const Parts p = parts(t);
    if (p.core.isEmpty())
        return false;
    for (int i = 0; i < p.core.size(); ++i) {
        const QChar c = p.core.at(i);
        const bool inner = i > 0 && i < p.core.size() - 1;
        if (!c.isLetter() && !(inner && (c == QLatin1Char('-') || c == QLatin1Char('\'') || c == QChar(0x2019))))
            return false;
    }
    if (word)
        *word = p.core;
    return true;
}

QStringList usefulModelWords(const QStringList &found, const QString &word, bool otherForms, const QStringList *near)
{
    static const QRegularExpression edges(QStringLiteral("^[\"“”‘'(\\[]+|[.,;:!?\"“”’')\\]]+$"));
    const QString lower = word.toLower();
    QSet<QString> bases{lower};
    if (!otherForms)
        for (const QString &b : baseForms(lower))
            bases.insert(b);
    QSet<QString> nearWords;
    if (near)
        for (const QString &n : *near)
            nearWords.insert(n.toLower());
    QStringList out;
    QSet<QString> seen{lower};
    for (QString w : found) {
        w = w.trimmed().remove(edges);
        const QString l = w.toLower();
        if (w.isEmpty() || seen.contains(l))
            continue;
        seen.insert(l);
        if (!otherForms) {
            bool sameWord = false;
            for (const QString &b : baseForms(l))
                sameWord = sameWord || bases.contains(b);
            if (sameWord)
                continue;
        }
        if (near && !nearWords.contains(l) && distance(l, lower, 2) > 2)
            continue;
        out << w;
    }
    return out;
}

} // namespace tp
