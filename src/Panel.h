#pragma once
// The panel of the add-on: the text to work on, the controls, and the result, in which each change is marked and
// every word can be clicked for the original and for other words. In the inspector the parts stand one above the
// other; in the add-on's window side by side (input left, output right) with the controls above and between.
#include "EngineApi.h"
#include "Logic.h"

#include <QPlainTextEdit>
#include <QPointer>
#include <QTextBrowser>
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
private:
    tp::Panel *m_panel;
};

// The result: read only; a click on a word opens its menu.
class OutputView : public QTextBrowser
{
    Q_OBJECT
public:
    explicit OutputView(QWidget *parent);
    int charAt(const QPoint &viewportPos) const;   // the document position of the character there, -1: none
signals:
    void wordClicked(int position, const QPoint &globalPos);
    void contextRequested(const QPoint &globalPos);
protected:
    void mousePressEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void contextMenuEvent(QContextMenuEvent *e) override;
private:
    QPoint m_pressPos;
};

class Panel : public QWidget
{
    Q_OBJECT
public:
    enum class Place { Inspector, Window };
    Panel(TranslateTool *tool, Place place, QWidget *parent);
    ~Panel() override;

    void setInput(const QString &text);
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
    void buildInspectorLayout();
    void buildWindowLayout();
    void updateModelBox();
    void setBusy(bool busy);
    void setStatus(const QString &text);
    void finishRun(const QStringList &results, const QString &error, double seconds, bool stopped);
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
};

} // namespace tp
