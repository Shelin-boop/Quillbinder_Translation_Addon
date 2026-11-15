#pragma once
// The add-on's text work that needs no model: which language a text is in, how it is cut into sentences, the
// writer's rules (no em-dashes, American spelling, quotes, own replacements), the safety checks on what a model
// wrote, and the word-by-word comparison that becomes the suggestions to accept or reject.
// Qt Core only: the checks in tests/ compile it alone.
#include <QHash>
#include <QList>
#include <QPair>
#include <QSet>
#include <QString>
#include <QStringList>

namespace tp {

// "de" or "en"
QString detectLanguage(const QString &text);

// One paragraph cut into sentences (a closing quote stays with its sentence).
QStringList splitSentences(const QString &paragraph);

// The pieces a text is worked in: each paragraph keeps its place (an empty line stays an empty line), and every
// sentence (German: a few sentences together, so that the model sees who "Ihr" is) becomes one unit.
struct Plan {
    QStringList units;               // the texts given to the model
    QList<QList<int>> paragraphs;    // per paragraph the indexes of its units; empty: an empty paragraph
    QStringList emptyParagraphs;     // the text of each paragraph without units (spaces), by paragraph index
};
Plan makePlan(const QString &text, bool groupForContext, int maxWords = 60);
// Joins the units again: units of a paragraph with one space, paragraphs with a line break.
QString joinPlan(const Plan &plan, const QStringList &unitTexts);

QString toPlainQuotes(const QString &text);    // „“”«» to ", ‚‘’ to '
QString toCurlyQuotes(const QString &text);    // American typography: “ ” and ’

struct Rules {
    bool noEmDash = true;        // no dash between words: the models may not write one, dashes become commas
    bool american = true;        // British spellings become American ones
    bool curlyQuotes = true;     // “ ” ‘ ’ instead of " '
    bool plainEllipsis = true;   // "..." instead of the single character …
    QList<QPair<QString, QString>> replacements;  // the writer's own: whole words, case kept
};
// The writer's own replacements from the options text: one per line, "old => new"; lines starting with # are notes.
QList<QPair<QString, QString>> parseReplacements(const QString &text, QStringList *errors = nullptr);

// Before the model: dashes between words become ", " (counted into *dashes).
QString beforeModel(const QString &text, const Rules &rules, int *dashes = nullptr);
// After the model: spelling, own replacements, ellipsis, quotes. Each change made goes into *notes once
// ("American spelling: grey -> gray").
QString afterModel(const QString &text, const Rules &rules, QStringList *notes = nullptr);

// The American spelling list (British\tAmerican per line), loaded once; tests may load another.
void loadSpellingList(const QString &file);
const QHash<QString, QString> &spellingList();

// Safety checks for a corrected sentence (both texts with plain quotes).
double similarity(const QString &a, const QString &b);           // 0..1, by characters (as difflib's ratio)
bool changedTooMuch(const QString &original, const QString &corrected, double minSimilarity = 0.75);
QSet<QString> doubledWords(const QString &text);                 // "the the" -> {"the"}
bool newDoubledWord(const QString &original, const QString &corrected);

// Word by word: the text is cut at spaces (punctuation stays with its word), and the two lists are compared.
QStringList words(const QString &text);
struct Segment {
    bool changed = false;
    QStringList before;   // the original's words (empty: words added)
    QStringList after;    // the corrected words (empty: words left out)
    bool accepted = true; // for a change: the writer takes it (false: the original stays)
};
QList<Segment> compareWords(const QString &original, const QString &corrected);
QString segmentsText(const QList<Segment> &segments);   // the text as the writer has decided

} // namespace tp
