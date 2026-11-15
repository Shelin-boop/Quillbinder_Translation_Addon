#pragma once
// The panel of the add-on: the text to work on, the controls, and the result, in which each change is marked and
// every word can be clicked for the original and for other words. In the inspector the parts stand one above the
// other; in the add-on's window side by side (input left, output right) with the controls above and between.
#include "EngineApi.h"
#include "Logic.h"

#include <QHash>
#include <QPlainTextEdit>
#include <QPointer>
#include <QTextBrowser>
#include <QTimer>
#include <QWidget>

#include <atomic>
#include <memory>

class QComboBox;
class QLabel;
class QMenu;
class QProgressBar;
class QPushButton;
class QSplitter;
class QToolButton;

namespace tp {

class TranslateTool;
class Panel;

// One request for other words, prepared in the main thread; the engine part may then run in any thread (it touches
// only the engine, which has its own lock).
struct AltJob {
    EngineInterface *engine = nullptr;
    QString slot;
    ModelFiles files;
    int threads = 0;
    QString source, current;
    int word = -1;
    RunOptions options;
};

// The input: the usual editing, plus "Other Words for ..." in its context menu (English text).
class InputEdit : public QPlainTextEdit
{
    Q_OBJECT
public:
    InputEdit(tp::Panel *panel, QWidget *parent);   // tp:: because QFrame has an enum value Panel
protected:
    void contextMenuEvent(QContextMenuEvent *e) override;
    void keyPressEvent(QKeyEvent *e) override;
    void mouseDoubleClickEvent(QMouseEvent *e) override;   // 1.1: the word goes into the information area
private:
    tp::Panel *m_panel;
};

// The result: read only; a click on a word opens its menu, a double click (1.1) shows it in the information area.
class OutputView : public QTextBrowser
{
    Q_OBJECT
public:
    explicit OutputView(QWidget *parent);
    int charAt(const QPoint &viewportPos) const;   // the document position of the character there, -1: none
signals:
    void wordClicked(int position, const QPoint &globalPos);
    void wordDoubleClicked(int position);
    void linkClicked(const QString &href);          // a word of the suggestions under a single word (1.1)
    void contextRequested(const QPoint &globalPos);
protected:
    void mousePressEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void mouseDoubleClickEvent(QMouseEvent *e) override;
    void contextMenuEvent(QContextMenuEvent *e) override;
private:
    QPoint m_pressPos;
    // a click waits for the double-click time: a double click is no click (its menu would cover the word)
    QTimer m_clickTimer;
    int m_clickPos = -1;
    QPoint m_clickGlobal;
};

class Panel : public QWidget
{
    Q_OBJECT
public:
    enum class Place { Inspector, Window };
    Panel(TranslateTool *tool, Place place, QWidget *parent);
    ~Panel() override;

    void setInput(const QString &text);   // (does not start live work: a run follows when wanted)
    QString inputText() const;
    QString resultText() const;        // the result as the writer has decided (what Copy and Into Editor give)
    bool isBusy() const { return m_busy; }
    void reloadSettings();             // models and rules after File > Options

    // the parts, for the tests
    InputEdit *input() const { return m_input; }
    OutputView *output() const { return m_output; }
    QString status() const;
    QString notes() const;
    int changeCount() const;
    // what a click on the word at that document position of the result offers (the tests use it without a menu)
    struct WordInfo {
        int unit = -1, word = -1, segment = -1;
        QString text;
    };
    WordInfo wordAt(int position) const;
    void setSegmentAccepted(int unit, int segment, bool accepted);
    void setAllAccepted(bool accepted);
    // other words for a word of the result or of the input; blocking variants for the tests
    QList<Alternative> outputAlternatives(int unit, int word, QString *error);
    void chooseOutputAlternative(int unit, const Alternative &a);
    struct InputPlace {
        int block = -1, start = -1, length = 0, word = -1;
        QString sentence, wordText;
    };
    InputPlace inputPlace(int position) const;
    QList<Alternative> inputAlternatives(const InputPlace &place, QString *error);
    void chooseInputAlternative(const InputPlace &place, const Alternative &a);

    // ---- 1.1
    // live work: about a second after the writer stops typing, the changed sentences are worked on again
    bool isLive() const;
    void setLive(bool on);
    QToolButton *liveButton() const { return m_live; }
    // the information area: a double click on a word of the input or the result shows its spelling suggestions,
    // words of a similar meaning and the model's other words
    QTextBrowser *info() const { return m_info; }
    void showInfoForOutput(int position);
    void showInfoForInput(int position);
    bool infoPending() const { return m_infoPending; }
    // a single word in the input: the word as it should be, and below it every suggestion for it
    struct SingleWord {
        bool on = false;
        QString word;           // as typed
        QString result;         // as it should be
        QStringList spelling;   // when it is misspelled
        QString base;           // the form the similar words are for ("walk" for "walked")
        QList<Similar> similar;
        QStringList modelWords; // the model's other words (they come a moment later)
        bool modelPending = false;
    };
    const SingleWord &singleWord() const { return m_single; }
    int cachedSentences() const { return int(m_cache.size()); }

public slots:
    void run();
    void stop();
    void takeFromEditor();
    void putIntoEditor();
    void copyResult();
    void showInputAlternatives(int position, const QPoint &globalPos);

signals:
    void finished();   // a run ended (also stopped or failed)

protected:
    void showEvent(QShowEvent *e) override;

private:
    struct Unit {
        QString original;    // what the writer wrote (this sentence or these sentences)
        QString modelInput;  // what the model got (rules before the model, plain quotes)
        QString raw;         // what the model wrote
        QList<Segment> segments;
        QString flag;        // why the original was kept
    };
    QString currentTask() const;          // "auto", "proofread", "translate"
    QString stylePrefix() const;
    // 1.1: what a chat model is told: "correct", "formal", "paraphrase" or "translate"
    static QString systemFor(const QString &what);
    static QStringList examplesFor(const QString &what);
    bool chatModel(const QString &modelId) const;
    void runNow(bool live);
    void liveTyped();
    void finishSingle();
    void requestSingleModelWords();
    void fillInfo();
    void requestInfoModelWords(const AltJob &job, bool ok, const QString &jobError, const QString &word);
    void buildInspectorLayout();
    void buildWindowLayout();
    void updateModelBox();
    void setBusy(bool busy);
    void setStatus(const QString &text);
    void finishRun(const QStringList &results, const QString &error, double seconds, bool stopped);
    void linkClicked(const QString &href);
    void render();
    QString unitText(int unit) const;
    void outputClicked(int position, const QPoint &globalPos);
    void outputContextMenu(const QPoint &globalPos);
    QString slot() const { return m_lang == QLatin1String("de") ? QStringLiteral("translator") : QStringLiteral("corrector"); }
    bool proofreading() const { return m_lang != QLatin1String("de"); }
    bool realWord(const QString &word) const;
    bool outputJob(int unit, int word, AltJob *job, QString *error);
    QList<Alternative> filterAlternatives(const QList<Alternative> &alts, const QString &currentWord) const;

    TranslateTool *m_tool;
    Place m_place;
    InputEdit *m_input = nullptr;
    OutputView *m_output = nullptr;
    QComboBox *m_task = nullptr, *m_style = nullptr, *m_model = nullptr;
    QPushButton *m_go = nullptr;
    QToolButton *m_from = nullptr, *m_into = nullptr, *m_copy = nullptr, *m_stop = nullptr, *m_models = nullptr;
    QLabel *m_status = nullptr, *m_notes = nullptr;
    QProgressBar *m_progress = nullptr;
    QSplitter *m_split = nullptr;
    QTextBrowser *m_info = nullptr;
    QToolButton *m_live = nullptr;

    QString m_lang = QStringLiteral("en");
    QString m_styleId;
    Plan m_plan;
    QList<Unit> m_units;
    QStringList m_noteList;
    bool m_busy = false;
    std::shared_ptr<std::atomic_bool> m_cancel;
    QPointer<QMenu> m_wordMenu;
    int m_request = 0;   // the latest alternatives request (older answers are dropped)
    bool m_sized = false;

    // 1.1
    QTimer m_liveTimer;
    bool m_rerun = false;           // the writer typed on while a run was busy: run again when it ends
    bool m_liveRun = false;         // the run was started by typing (no "There is no text" message)
    QList<int> m_sent;              // the units given to the model in this run (the others came from the cache)
    QHash<QString, QString> m_cache;   // what the model wrote for a text, by model, instruction and text
    QString m_runKey;               // model and instruction of this run, for the cache
    SingleWord m_single;
    int m_singleRequest = 0;
    // the information area's word
    QString m_infoWord;
    QString m_infoContext;          // "input" or "output", and whether the word is English
    bool m_infoEnglish = true;
    QStringList m_infoModelWords;
    QString m_infoModelError;
    bool m_infoPending = false;
    int m_infoRequest = 0;
};

} // namespace tp
