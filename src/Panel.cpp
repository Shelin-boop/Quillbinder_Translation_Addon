#include "Panel.h"

#include "TranslateTool.h"

#include <QAbstractTextDocumentLayout>
#include <algorithm>
#include <QApplication>
#include <QBoxLayout>
#include <QClipboard>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QGridLayout>
#include <QGuiApplication>
#include <QLabel>
#include <QMenu>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollBar>
#include <QSplitter>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QThread>
#include <QToolButton>

namespace tp {

namespace {

enum Prop {
    UnitProp = QTextFormat::UserProperty + 1,
    WordProp,
    SegmentProp,
};

const QString kCorrect = QStringLiteral("Fix grammatical errors in this sentence: ");
const QString kFormal = QStringLiteral("Make this sentence more formal: ");
const QString kParaphrase = QStringLiteral("Paraphrase this sentence: ");

const QSet<QString> &englishWordList()
{
    static const QSet<QString> words = [] {
        QSet<QString> out;
        QFile f(QStringLiteral(":/translation-proofreading/en-words.txt"));
        if (f.open(QIODevice::ReadOnly))
            for (const QByteArray &line : f.readAll().split('\n'))
                if (!line.isEmpty())
                    out.insert(QString::fromUtf8(line));
        return out;
    }();
    return words;
}

QList<Alternative> runAltJob(const AltJob &j, QString *error)
{
    if (!j.engine || !j.engine->load(j.slot, j.files, j.threads, error))
        return {};
    return j.engine->alternatives(j.slot, j.source, j.current, j.word, j.options, 7, error);
}

QIcon themed(const QString &name)
{
    return QIcon::fromTheme(name);
}

QToolButton *toolButton(const QString &icon, const QString &text, const QString &tip, QWidget *parent)
{
    auto *b = new QToolButton(parent);
    b->setIcon(themed(icon));
    b->setText(text);
    b->setToolTip(tip);
    b->setToolButtonStyle(b->icon().isNull() ? Qt::ToolButtonTextOnly : Qt::ToolButtonTextBesideIcon);
    b->setAutoRaise(true);
    return b;
}

} // namespace

// ---------------------------------------------------------------- InputEdit

InputEdit::InputEdit(tp::Panel *panel, QWidget *parent)
    : QPlainTextEdit(parent)
    , m_panel(panel)
{
    setObjectName(QStringLiteral("tpInput"));
    setPlaceholderText(tr("Type or paste text here, or take the selected text from the editor. English text is "
                          "checked for spelling and grammar, German text is translated into English."));
}

void InputEdit::contextMenuEvent(QContextMenuEvent *e)
{
    QMenu *menu = createStandardContextMenu(e->pos());
    const QTextCursor c = cursorForPosition(e->pos());
    const tp::Panel::InputPlace place = m_panel->inputPlace(c.position());
    if (!place.wordText.isEmpty()) {
        QAction *first = menu->actions().value(0);
        QAction *ac = new QAction(tr("Other Words for “%1”...").arg(place.wordText), menu);
        const int pos = c.position();
        const QPoint global = e->globalPos();
        connect(ac, &QAction::triggered, m_panel, [this, pos, global] { m_panel->showInputAlternatives(pos, global); });
        menu->insertAction(first, ac);
        menu->insertSeparator(first);
    }
    menu->exec(e->globalPos());
    delete menu;
}

void InputEdit::keyPressEvent(QKeyEvent *e)
{
    if ((e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) && (e->modifiers() & Qt::ControlModifier)) {
        m_panel->run();
        return;
    }
    QPlainTextEdit::keyPressEvent(e);
}

// ---------------------------------------------------------------- OutputView

OutputView::OutputView(QWidget *parent)
    : QTextBrowser(parent)
{
    setObjectName(QStringLiteral("tpOutput"));
    setOpenLinks(false);
    setPlaceholderText(tr("The result. Marked words were changed: click one to keep the original or to choose "
                          "another word. Every other word can be clicked for other words, too."));
    viewport()->setCursor(Qt::PointingHandCursor);
}

int OutputView::charAt(const QPoint &viewportPos) const
{
    const QPoint docPos = viewportPos + QPoint(horizontalScrollBar()->value(), verticalScrollBar()->value());
    return document()->documentLayout()->hitTest(docPos, Qt::ExactHit);
}

void OutputView::mousePressEvent(QMouseEvent *e)
{
    m_pressPos = e->pos();
    QTextBrowser::mousePressEvent(e);
}

void OutputView::mouseReleaseEvent(QMouseEvent *e)
{
    QTextBrowser::mouseReleaseEvent(e);
    if (e->button() != Qt::LeftButton || (e->pos() - m_pressPos).manhattanLength() > 4 || textCursor().hasSelection())
        return;
    const int pos = charAt(e->pos());
    if (pos >= 0)
        emit wordClicked(pos, e->globalPosition().toPoint());
}

void OutputView::contextMenuEvent(QContextMenuEvent *e)
{
    emit contextRequested(e->globalPos());
}

// ---------------------------------------------------------------- Panel

Panel::Panel(TranslateTool *tool, Place place, QWidget *parent)
    : QWidget(parent)
    , m_tool(tool)
    , m_place(place)
{
    setObjectName(place == Place::Window ? QStringLiteral("tpWindowPanel") : QStringLiteral("tpInspectorPanel"));
    m_input = new InputEdit(this, this);
    m_output = new OutputView(this);
    connect(m_output, &OutputView::wordClicked, this, &Panel::outputClicked);
    connect(m_output, &OutputView::contextRequested, this, &Panel::outputContextMenu);

    m_task = new QComboBox(this);
    m_task->setObjectName(QStringLiteral("tpTask"));
    m_task->addItem(tr("Automatic"), QStringLiteral("auto"));
    m_task->addItem(tr("Proofread English"), QStringLiteral("proofread"));
    m_task->addItem(tr("Translate German to English"), QStringLiteral("translate"));
    m_task->setToolTip(tr("Automatic: German text is translated into English, English text is proofread."));
    m_style = new QComboBox(this);
    m_style->setObjectName(QStringLiteral("tpStyle"));
    m_style->addItem(tr("Correct"), QStringLiteral("correct"));
    m_style->addItem(tr("More Formal"), QStringLiteral("formal"));
    m_style->setToolTip(tr("Correct: spelling and grammar only. More Formal: the model may also rewrite for a more "
                           "formal tone."));
    m_model = new QComboBox(this);
    m_model->setObjectName(QStringLiteral("tpModel"));
    m_model->setToolTip(tr("The proofreading model (choose and download models in the options)"));
    connect(m_model, &QComboBox::activated, this, [this](int i) {
        const QString id = m_model->itemData(i).toString();
        if (!id.isEmpty())
            m_tool->setSetting(QStringLiteral("corrector"), id);
    });
    m_models = toolButton(QStringLiteral("configure"), tr("Models..."), tr("Choose and download models, set the rules"), this);
    connect(m_models, &QToolButton::clicked, m_tool, &TranslateTool::showOptions);

    m_go = new QPushButton(this);
    m_go->setObjectName(QStringLiteral("tpGo"));
    m_go->setDefault(true);
    m_go->setToolTip(tr("Proofread or translate the text (Ctrl+Return in the text)"));
    connect(m_go, &QPushButton::clicked, this, &Panel::run);
    m_stop = toolButton(QStringLiteral("media-playback-stop"), tr("Stop"), tr("Stop the work"), this);
    m_stop->setVisible(false);
    connect(m_stop, &QToolButton::clicked, this, &Panel::stop);
    m_from = toolButton(QStringLiteral("go-up"), tr("From Editor"), tr("Take the text selected in the editor and work on it"), this);
    connect(m_from, &QToolButton::clicked, this, &Panel::takeFromEditor);
    m_into = toolButton(QStringLiteral("go-down"), tr("Into Editor"),
                        tr("Put the result into the editor in place of its selection (one step to undo there)"), this);
    connect(m_into, &QToolButton::clicked, this, &Panel::putIntoEditor);
    m_copy = toolButton(QStringLiteral("edit-copy"), tr("Copy"), tr("Copy the result"), this);
    connect(m_copy, &QToolButton::clicked, this, &Panel::copyResult);

    m_progress = new QProgressBar(this);
    m_progress->setVisible(false);
    m_progress->setTextVisible(false);
    m_progress->setMaximumHeight(8);
    m_status = new QLabel(this);
    m_status->setObjectName(QStringLiteral("tpStatus"));
    m_status->setWordWrap(true);
    m_status->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_notes = new QLabel(this);
    m_notes->setObjectName(QStringLiteral("tpNotes"));
    m_notes->setWordWrap(true);
    m_notes->setVisible(false);
    QFont small = m_notes->font();
    small.setPointSizeF(small.pointSizeF() * 0.9);
    m_notes->setFont(small);

    if (m_place == Place::Window)
        buildWindowLayout();
    else
        buildInspectorLayout();
    updateModelBox();
    setStatus(tr("Ready."));
}

Panel::~Panel()
{
    if (m_cancel)
        m_cancel->store(true);
}

void Panel::buildInspectorLayout()
{
    m_go->setText(tr("Check"));
    if (QFileInfo::exists(m_tool->iconPath()))
        m_go->setIcon(QIcon(m_tool->iconPath()));
    auto *controls = new QWidget(this);
    auto *g = new QGridLayout(controls);
    g->setContentsMargins(0, 4, 0, 4);
    g->setVerticalSpacing(3);
    g->addWidget(m_task, 0, 0, 1, 2);
    g->addWidget(m_style, 1, 0);
    g->addWidget(m_model, 1, 1);
    auto *buttons = new QHBoxLayout;
    buttons->setSpacing(2);
    buttons->addWidget(m_go);
    buttons->addWidget(m_stop);
    buttons->addStretch(1);
    buttons->addWidget(m_from);
    buttons->addWidget(m_into);
    buttons->addWidget(m_copy);
    buttons->addWidget(m_models);
    for (QToolButton *b : {m_from, m_into, m_copy, m_models})
        if (!b->icon().isNull())   // without the program's icons the words stay
            b->setToolButtonStyle(Qt::ToolButtonIconOnly);
    g->addLayout(buttons, 2, 0, 1, 2);
    g->addWidget(m_progress, 3, 0, 1, 2);
    g->addWidget(m_status, 4, 0, 1, 2);
    // room for two lines of status: the splitter must not cut the second one off
    m_status->setMinimumHeight(2 * m_status->fontMetrics().lineSpacing() + 2);
    m_status->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    g->setColumnStretch(0, 1);
    g->setColumnStretch(1, 1);

    auto *out = new QWidget(this);
    auto *ol = new QVBoxLayout(out);
    ol->setContentsMargins(0, 0, 0, 0);
    ol->addWidget(m_output, 1);
    ol->addWidget(m_notes);

    m_split = new QSplitter(Qt::Vertical, this);
    m_split->addWidget(m_input);
    m_split->addWidget(controls);
    m_split->addWidget(out);
    m_split->setCollapsible(1, false);
    m_split->setStretchFactor(0, 2);
    m_split->setStretchFactor(1, 0);
    m_split->setStretchFactor(2, 3);
    auto *l = new QVBoxLayout(this);
    l->setContentsMargins(2, 2, 2, 2);
    l->addWidget(m_split);
}

void Panel::buildWindowLayout()
{
    m_go->setIcon(themed(QStringLiteral("go-next")));
    m_go->setText(m_go->icon().isNull() ? QStringLiteral("→") : QString());
    m_go->setIconSize(QSize(32, 32));
    m_go->setFixedSize(56, 48);
    auto *top = new QHBoxLayout;
    top->addWidget(m_task);
    top->addWidget(m_style);
    top->addWidget(m_model);
    top->addWidget(m_models);
    top->addStretch(1);
    top->addWidget(m_from);
    top->addWidget(m_into);
    top->addWidget(m_copy);

    auto *middle = new QWidget(this);
    auto *ml = new QVBoxLayout(middle);
    ml->setContentsMargins(4, 0, 4, 0);
    ml->addStretch(1);
    ml->addWidget(m_go, 0, Qt::AlignHCenter);
    ml->addWidget(m_stop, 0, Qt::AlignHCenter);
    ml->addStretch(1);

    auto *out = new QWidget(this);
    auto *ol = new QVBoxLayout(out);
    ol->setContentsMargins(0, 0, 0, 0);
    ol->addWidget(m_output, 1);
    ol->addWidget(m_notes);

    m_split = new QSplitter(Qt::Horizontal, this);
    m_split->addWidget(m_input);
    m_split->addWidget(middle);
    m_split->addWidget(out);
    m_split->setCollapsible(1, false);
    m_split->setStretchFactor(0, 1);
    m_split->setStretchFactor(1, 0);
    m_split->setStretchFactor(2, 1);

    auto *l = new QVBoxLayout(this);
    l->setContentsMargins(6, 6, 6, 6);
    l->addLayout(top);
    l->addWidget(m_split, 1);
    l->addWidget(m_progress);
    l->addWidget(m_status);
}

void Panel::showEvent(QShowEvent *e)
{
    QWidget::showEvent(e);
    // the window: input and result the same width at first
    if (m_place == Place::Window && !m_sized && m_split && width() > 200) {
        m_sized = true;
        const int middle = 72;
        const int half = qMax(100, (m_split->width() - middle) / 2);
        m_split->setSizes({half, middle, half});
    }
}

void Panel::reloadSettings()
{
    updateModelBox();
}

void Panel::updateModelBox()
{
    m_model->clear();
    const QString chosen = m_tool->chosenModel(QStringLiteral("corrector"));
    for (const CatalogModel &m : m_tool->store()->models(QStringLiteral("corrector")))
        if (m_tool->store()->isInstalled(m.id))
            m_model->addItem(m.title, m.id);
    if (m_model->count() == 0) {
        m_model->addItem(tr("No proofreading model yet"), QString());
        m_model->setEnabled(false);
    } else {
        m_model->setEnabled(true);
        m_model->setCurrentIndex(qMax(0, m_model->findData(chosen)));
    }
}

QString Panel::currentTask() const
{
    return m_task->currentData().toString();
}

QString Panel::stylePrefix() const
{
    return m_styleId == QLatin1String("formal") ? kFormal : kCorrect;
}

void Panel::setInput(const QString &text)
{
    m_input->setPlainText(text);
}

QString Panel::inputText() const
{
    return m_input->toPlainText();
}

QString Panel::status() const
{
    return m_status->text();
}

QString Panel::notes() const
{
    return m_notes->text();
}

void Panel::setStatus(const QString &text)
{
    m_status->setText(text);
}

void Panel::setBusy(bool busy)
{
    m_busy = busy;
    m_go->setEnabled(!busy);
    m_stop->setVisible(busy);
    m_progress->setVisible(busy);
    m_task->setEnabled(!busy);
    m_style->setEnabled(!busy);
    m_from->setEnabled(!busy);
}

void Panel::run()
{
    if (m_busy)
        return;
    const QString text = inputText();
    if (text.trimmed().isEmpty()) {
        setStatus(tr("There is no text to work on yet."));
        emit finished();
        return;
    }
    const QString task = currentTask();
    m_lang = task == QLatin1String("translate") ? QStringLiteral("de")
             : task == QLatin1String("proofread") ? QStringLiteral("en")
                                                  : detectLanguage(text);
    m_styleId = m_style->currentData().toString();
    const QString kind = slot();
    const QString modelId = m_tool->chosenModel(kind);
    if (modelId.isEmpty()) {
        setStatus(kind == QLatin1String("translator")
                      ? tr("No translation model is downloaded yet. Choose one with Models... (File > Options > "
                           "Proofreading).")
                      : tr("No proofreading model is downloaded yet. Choose one with Models... (File > Options > "
                           "Proofreading)."));
        emit finished();
        return;
    }
    QString error;
    EngineInterface *engine = m_tool->engine(&error);
    if (!engine) {
        setStatus(tr("The engine cannot be used: %1").arg(error));
        emit finished();
        return;
    }
    const Rules rules = m_tool->rules();
    m_plan = makePlan(text, !proofreading());
    m_units.clear();
    m_noteList.clear();
    int dashes = 0;
    QStringList modelTexts;
    for (const QString &u : m_plan.units) {
        Unit unit;
        unit.original = u;
        int n = 0;
        unit.modelInput = toPlainQuotes(beforeModel(u, rules, &n));
        dashes += n;
        modelTexts << unit.modelInput;
        m_units << unit;
    }
    if (dashes)
        m_noteList << (dashes == 1 ? tr("1 dash replaced by a comma (rule: no em-dashes): click it to keep the dash.")
                                   : tr("%1 dashes replaced (rule: no em-dashes): click one to keep the dash.").arg(dashes));

    RunOptions o;
    o.prefix = proofreading() ? stylePrefix() : QString();
    // beam 4: with 2 the large corrector turned "bellow" into "downstairs" (test of 2026-11-15)
    o.beam = m_tool->setting(QStringLiteral("careful"), false).toBool() ? 6 : 4;
    o.banDashes = rules.noEmDash;
    o.batch = 8;
    const ModelFiles files = m_tool->store()->files(modelId);
    const int threads = m_tool->threads();
    const QString slotName = kind;
    m_cancel = std::make_shared<std::atomic_bool>(false);
    auto cancel = m_cancel;
    setBusy(true);
    m_progress->setRange(0, 0);
    const QString title = m_tool->store()->model(modelId) ? m_tool->store()->model(modelId)->title : modelId;
    setStatus(engine->loaded(slotName) == modelId ? tr("Working...") : tr("Loading %1...").arg(title));
    QPointer<Panel> self(this);
    QThread *t = QThread::create([self, engine, files, threads, slotName, modelTexts, o, cancel] {
        QElapsedTimer clock;
        clock.start();
        QString error;
        QStringList results;
        if (engine->load(slotName, files, threads, &error)) {
            results = engine->run(slotName, modelTexts, o,
                                  [self, cancel](int done, int total) {
                                      if (self)
                                          QMetaObject::invokeMethod(self.data(), [self, done, total] {
                                              if (!self)
                                                  return;
                                              self->m_progress->setRange(0, total);
                                              self->m_progress->setValue(done);
                                              self->setStatus(tr("Working: %1 of %2 sentences...").arg(done).arg(total));
                                          }, Qt::QueuedConnection);
                                      return !cancel->load();
                                  },
                                  &error);
        }
        const double seconds = clock.elapsed() / 1000.0;
        const bool stopped = cancel->load();
        if (self)
            QMetaObject::invokeMethod(self.data(), [self, results, error, seconds, stopped] {
                if (self)
                    self->finishRun(results, error, seconds, stopped);
            }, Qt::QueuedConnection);
    });
    connect(t, &QThread::finished, t, &QObject::deleteLater);
    t->start();
}

void Panel::stop()
{
    if (m_cancel)
        m_cancel->store(true);
    setStatus(tr("Stopping after the sentences being worked on..."));
}

void Panel::finishRun(const QStringList &results, const QString &error, double seconds, bool stopped)
{
    setBusy(false);
    m_tool->touchEngine();
    if (!error.isEmpty() || results.size() != m_units.size()) {
        setStatus(tr("The work failed: %1").arg(error.isEmpty() ? tr("no result") : error));
        emit finished();
        return;
    }
    const Rules rules = m_tool->rules();
    int kept = 0;
    for (int i = 0; i < m_units.size(); ++i) {
        Unit &u = m_units[i];
        QString raw = results.at(i);
        if (raw.isEmpty()) // stopped before this sentence
            raw = u.modelInput;
        u.raw = raw;
        if (proofreading() && m_styleId == QLatin1String("correct")) {
            if (changedTooMuch(u.modelInput, raw)) {
                u.flag = tr("The model rewrote this sentence too much; your sentence was kept. (It wrote: %1)").arg(raw);
                u.raw = u.modelInput;
                ++kept;
            } else if (newDoubledWord(u.modelInput, raw)) {
                u.flag = tr("The model repeated a word; your sentence was kept. (It wrote: %1)").arg(raw);
                u.raw = u.modelInput;
                ++kept;
            }
        }
        const QString shown = afterModel(u.raw, rules, &m_noteList);
        if (proofreading()) {
            u.segments = compareWords(u.original, shown);
        } else {
            Segment s;
            s.before = s.after = words(shown);
            u.segments = {s};
        }
    }
    if (kept)
        m_noteList << (kept == 1 ? tr("1 sentence kept as you wrote it: the model's version did not look right "
                                      "(point at the sentence to see what it wrote).")
                                 : tr("%1 sentences kept as you wrote them: the model's versions did not look right "
                                      "(point at a sentence to see what it wrote).").arg(kept));
    render();
    if (stopped)
        setStatus(tr("Stopped. The sentences done so far are shown; the rest is as you wrote it."));
    else if (proofreading())
        setStatus(changeCount() == 0 ? tr("Done in %1 s: no changes.").arg(seconds, 0, 'f', 1)
                  : changeCount() == 1
                      ? tr("Done in %1 s: 1 change. Click it for the original and other words.").arg(seconds, 0, 'f', 1)
                      : tr("Done in %1 s: %2 changes. Click one for the original and other words.")
                            .arg(seconds, 0, 'f', 1)
                            .arg(changeCount()));
    else
        setStatus(tr("Translated in %1 s. Click a word for other words.").arg(seconds, 0, 'f', 1));
    emit finished();
}

int Panel::changeCount() const
{
    int n = 0;
    for (const Unit &u : m_units)
        for (const Segment &s : u.segments)
            n += s.changed ? 1 : 0;
    return n;
}

QString Panel::unitText(int unit) const
{
    return segmentsText(m_units.value(unit).segments);
}

QString Panel::resultText() const
{
    QStringList texts;
    for (int i = 0; i < m_units.size(); ++i)
        texts << unitText(i);
    return m_units.isEmpty() ? QString() : joinPlan(m_plan, texts);
}

void Panel::render()
{
    const int scroll = m_output->verticalScrollBar()->value();
    QTextDocument *doc = m_output->document();
    doc->clear();
    QTextCursor c(doc);
    const QPalette pal = m_output->palette();
    QTextCharFormat plain;
    plain.setForeground(pal.color(QPalette::Text));
    QTextCharFormat changed = plain;
    QColor mark(255, 196, 0, 110);
    changed.setBackground(mark);
    QTextCharFormat kept = plain;
    kept.setUnderlineStyle(QTextCharFormat::DotLine);
    kept.setUnderlineColor(QColor(230, 150, 0));
    QTextCharFormat flagged = plain;
    flagged.setUnderlineStyle(QTextCharFormat::WaveUnderline);
    flagged.setUnderlineColor(QColor(150, 150, 150));

    for (int p = 0; p < m_plan.paragraphs.size(); ++p) {
        if (p > 0)
            c.insertBlock();
        const QList<int> &idx = m_plan.paragraphs.at(p);
        if (idx.isEmpty()) {
            c.insertText(m_plan.emptyParagraphs.value(p), plain);
            continue;
        }
        for (int k = 0; k < idx.size(); ++k) {
            const int u = idx.at(k);
            const Unit &unit = m_units.at(u);
            if (k > 0)
                c.insertText(QStringLiteral(" "), plain);
            int word = 0;
            bool firstWord = true;
            for (int s = 0; s < unit.segments.size(); ++s) {
                const Segment &seg = unit.segments.at(s);
                const QStringList shown = seg.changed && !seg.accepted ? seg.before : seg.after;
                QTextCharFormat f = !seg.changed ? (unit.flag.isEmpty() ? plain : flagged) : seg.accepted ? changed : kept;
                f.setProperty(UnitProp, u);
                f.setProperty(SegmentProp, seg.changed ? s : -1);
                if (seg.changed) {
                    f.setToolTip(seg.accepted ? (seg.before.isEmpty() ? tr("Added. Click to leave it out.")
                                                                      : tr("Was: %1").arg(seg.before.join(QLatin1Char(' '))))
                                              : tr("Suggested: %1").arg(seg.after.isEmpty() ? tr("(leave out)")
                                                                                             : seg.after.join(QLatin1Char(' '))));
                } else if (!unit.flag.isEmpty()) {
                    f.setToolTip(unit.flag);
                }
                if (shown.isEmpty()) {
                    // a word left out (or an added one not taken): a small mark to click
                    if (!firstWord)
                        c.insertText(QStringLiteral(" "), plain);
                    QTextCharFormat m = f;
                    m.setProperty(WordProp, -1);
                    c.insertText(QStringLiteral("‸"), m);
                    firstWord = false;
                    continue;
                }
                for (const QString &w : shown) {
                    if (!firstWord) {
                        QTextCharFormat space = plain;
                        space.setProperty(UnitProp, u);
                        c.insertText(QStringLiteral(" "), space);
                    }
                    QTextCharFormat wf = f;
                    wf.setProperty(WordProp, word++);
                    c.insertText(w, wf);
                    firstWord = false;
                }
            }
        }
    }
    m_output->verticalScrollBar()->setValue(scroll);
    m_notes->setText(m_noteList.join(QLatin1Char('\n')));
    m_notes->setVisible(!m_noteList.isEmpty());
}

Panel::WordInfo Panel::wordAt(int position) const
{
    WordInfo info;
    QTextCursor c(m_output->document());
    c.setPosition(position);
    if (!c.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor))
        return info;
    const QTextCharFormat f = c.charFormat();
    if (!f.hasProperty(UnitProp))
        return info;
    info.unit = f.property(UnitProp).toInt();
    info.word = f.hasProperty(WordProp) ? f.property(WordProp).toInt() : -1;
    info.segment = f.hasProperty(SegmentProp) ? f.property(SegmentProp).toInt() : -1;
    if (info.word >= 0)
        info.text = words(unitText(info.unit)).value(info.word);
    return info;
}

void Panel::setSegmentAccepted(int unit, int segment, bool accepted)
{
    if (unit < 0 || unit >= m_units.size() || segment < 0 || segment >= m_units[unit].segments.size())
        return;
    m_units[unit].segments[segment].accepted = accepted;
    render();
}

void Panel::setAllAccepted(bool accepted)
{
    for (Unit &u : m_units)
        for (Segment &s : u.segments)
            s.accepted = accepted;
    render();
}

bool Panel::realWord(const QString &word) const
{
    QString letters;
    for (QChar ch : word)
        if (ch.isLetter() || ch == QLatin1Char('\'') || ch == QChar(0x2019))
            letters += ch == QChar(0x2019) ? QLatin1Char('\'') : ch;
    while (letters.endsWith(QLatin1Char('\'')))
        letters.chop(1);
    if (letters.isEmpty())
        return false;
    // a single letter is a piece of a word, except "a" and "I"
    if (letters.size() == 1 && letters != QLatin1String("a") && letters != QLatin1String("A") && letters != QLatin1String("I"))
        return false;
    const QSet<QString> &list = englishWordList();
    if (list.isEmpty())
        return true; // no list: nothing can be judged
    if (list.contains(letters) || list.contains(letters.toLower()))
        return true;
    // "don't", "she'll": the part before the apostrophe
    const int apo = letters.indexOf(QLatin1Char('\''));
    return apo > 0 && list.contains(letters.left(apo).toLower());
}

bool Panel::outputJob(int unit, int word, AltJob *job, QString *error)
{
    if (unit < 0 || unit >= m_units.size() || word < 0)
        return false;
    const QString modelId = m_tool->chosenModel(slot());
    if (modelId.isEmpty()) {
        if (error)
            *error = tr("no model downloaded");
        return false;
    }
    job->engine = m_tool->engine(error);
    if (!job->engine)
        return false;
    job->slot = slot();
    job->files = m_tool->store()->files(modelId);
    job->threads = m_tool->threads();
    job->source = m_units.at(unit).modelInput;
    job->current = toPlainQuotes(unitText(unit));
    job->word = word;
    job->options.prefix = proofreading() ? stylePrefix() : QString();
    job->options.banDashes = m_tool->rules().noEmDash;
    return true;
}

QList<Alternative> Panel::filterAlternatives(const QList<Alternative> &alts, const QString &currentWord) const
{
    QList<Alternative> out;
    for (const Alternative &a : alts) {
        if (a.word == currentWord || !realWord(a.word))
            continue;
        out << a;
        if (out.size() >= 6)
            break;
    }
    return out;
}

QList<Alternative> Panel::outputAlternatives(int unit, int word, QString *error)
{
    AltJob job;
    if (!outputJob(unit, word, &job, error))
        return {};
    const QList<Alternative> out = filterAlternatives(runAltJob(job, error), words(job.current).value(word));
    m_tool->touchEngine();
    return out;
}

void Panel::chooseOutputAlternative(int unit, const Alternative &a)
{
    if (unit < 0 || unit >= m_units.size())
        return;
    Unit &u = m_units[unit];
    u.raw = a.sentence;
    u.flag.clear();
    QStringList notes;
    const QString shown = afterModel(a.sentence, m_tool->rules(), &notes);
    if (proofreading()) {
        u.segments = compareWords(u.original, shown);
    } else {
        Segment s;
        s.before = s.after = words(shown);
        u.segments = {s};
    }
    render();
}

void Panel::outputClicked(int position, const QPoint &globalPos)
{
    if (m_busy)
        return;
    const WordInfo info = wordAt(position);
    if (info.unit < 0)
        return;
    if (m_wordMenu)
        m_wordMenu->close();
    auto *menu = new QMenu(this);
    menu->setObjectName(QStringLiteral("tpWordMenu"));
    menu->setAttribute(Qt::WA_DeleteOnClose);
    m_wordMenu = menu;
    const Unit &unit = m_units.at(info.unit);
    if (info.segment >= 0) {
        const Segment &seg = unit.segments.at(info.segment);
        const QString before = seg.before.join(QLatin1Char(' ')), after = seg.after.join(QLatin1Char(' '));
        QAction *use = menu->addAction(after.isEmpty() ? tr("Leave Out “%1”").arg(before)
                                                       : tr("Use “%1”").arg(after));
        use->setCheckable(true);
        use->setChecked(seg.accepted);
        QAction *keep = menu->addAction(before.isEmpty() ? tr("Do Not Add “%1”").arg(after)
                                                         : tr("Keep “%1”").arg(before));
        keep->setCheckable(true);
        keep->setChecked(!seg.accepted);
        const int u = info.unit, s = info.segment;
        connect(use, &QAction::triggered, this, [this, u, s] { setSegmentAccepted(u, s, true); });
        connect(keep, &QAction::triggered, this, [this, u, s] { setSegmentAccepted(u, s, false); });
    }
    if (!unit.flag.isEmpty() && info.segment < 0) {
        QAction *why = menu->addAction(tr("Your Sentence Was Kept"));
        why->setEnabled(false);
        why->setToolTip(unit.flag);
    }
    if (info.word >= 0) {
        if (!menu->isEmpty())
            menu->addSeparator();
        QAction *wait = menu->addAction(tr("Looking for other words..."));
        wait->setEnabled(false);
        const int request = ++m_request;
        const int u = info.unit;
        AltJob job;
        QString jobError;
        const bool ok = outputJob(info.unit, info.word, &job, &jobError);
        const QString currentWord = words(job.current).value(info.word);
        // the original words of a change are offered by "Keep" already
        const QString original = info.segment >= 0 ? unit.segments.at(info.segment).before.join(QLatin1Char(' ')) : QString();
        QPointer<Panel> self(this);
        QPointer<QMenu> menuGuard(menu);
        // the model's answer takes up to a few seconds: the menu is open meanwhile and gets the words when they come
        QThread *t = QThread::create([self, job, ok, jobError, request, menuGuard, u, currentWord, original] {
            QString error = jobError;
            const QList<Alternative> found = ok ? runAltJob(job, &error) : QList<Alternative>();
            QMetaObject::invokeMethod(qApp, [self, found, error, request, menuGuard, u, currentWord, original] {
                if (!self || !menuGuard || request != self->m_request)
                    return;
                self->m_tool->touchEngine();
                QList<Alternative> alts = self->filterAlternatives(found, currentWord);
                alts.erase(std::remove_if(alts.begin(), alts.end(), [&](const Alternative &a) { return a.word == original; }),
                           alts.end());
                QMenu *m = menuGuard.data();
                if (QAction *last = m->actions().isEmpty() ? nullptr : m->actions().last(); last && !last->isEnabled())
                    m->removeAction(last);
                if (alts.isEmpty()) {
                    QAction *none = m->addAction(error.isEmpty() ? tr("No other words found") : tr("No other words: %1").arg(error));
                    none->setEnabled(false);
                }
                for (const Alternative &a : alts) {
                    QAction *ac = m->addAction(a.word);
                    ac->setToolTip(a.sentence);
                    QObject::connect(ac, &QAction::triggered, self.data(), [self, u, a] {
                        if (self)
                            self->chooseOutputAlternative(u, a);
                    });
                }
                m->adjustSize();
            }, Qt::QueuedConnection);
        });
        connect(t, &QThread::finished, t, &QObject::deleteLater);
        t->start();
    }
    if (menu->isEmpty()) {
        delete menu;
        return;
    }
    menu->setToolTipsVisible(true);
    menu->popup(globalPos);
}

void Panel::outputContextMenu(const QPoint &globalPos)
{
    QMenu menu(this);
    QAction *copySel = menu.addAction(tr("Copy Selection"), m_output, &QTextBrowser::copy);
    copySel->setEnabled(m_output->textCursor().hasSelection());
    menu.addAction(tr("Copy the Result"), this, &Panel::copyResult)->setEnabled(!m_units.isEmpty());
    if (proofreading() && changeCount() > 0) {
        menu.addSeparator();
        menu.addAction(tr("Use All Suggestions"), this, [this] { setAllAccepted(true); });
        menu.addAction(tr("Keep All Originals"), this, [this] { setAllAccepted(false); });
    }
    menu.exec(globalPos);
}

Panel::InputPlace Panel::inputPlace(int position) const
{
    InputPlace place;
    const QTextBlock block = m_input->document()->findBlock(position);
    if (!block.isValid())
        return place;
    const QString text = block.text();
    const int offset = position - block.position();
    int from = 0;
    for (const QString &s : splitSentences(text)) {
        const int start = text.indexOf(s, from);
        if (start < 0)
            continue;
        from = start + s.size();
        if (offset < start || offset > start + s.size())
            continue;
        place.block = block.blockNumber();
        place.start = start;
        place.length = s.size();
        place.sentence = s;
        // the word under the position: count the words before it
        const QString before = s.left(offset - start);
        const QStringList ws = words(s);
        int index = words(before).size();
        if (!before.isEmpty() && !before.at(before.size() - 1).isSpace() && index > 0)
            --index; // inside a word
        if (index >= 0 && index < ws.size()) {
            place.word = index;
            place.wordText = ws.at(index);
        }
        break;
    }
    // other words only for English text
    if (detectLanguage(text) == QLatin1String("de"))
        place.wordText.clear();
    return place;
}

QList<Alternative> Panel::inputAlternatives(const InputPlace &place, QString *error)
{
    if (place.word < 0)
        return {};
    const QString modelId = m_tool->chosenModel(QStringLiteral("corrector"));
    if (modelId.isEmpty()) {
        if (error)
            *error = tr("no proofreading model downloaded");
        return {};
    }
    AltJob job;
    job.engine = m_tool->engine(error);
    if (!job.engine)
        return {};
    job.slot = QStringLiteral("corrector");
    job.files = m_tool->store()->files(modelId);
    job.threads = m_tool->threads();
    job.source = job.current = toPlainQuotes(place.sentence);
    job.word = place.word;
    job.options.prefix = kParaphrase;
    job.options.banDashes = m_tool->rules().noEmDash;
    const QList<Alternative> out = filterAlternatives(runAltJob(job, error), words(job.current).value(place.word));
    m_tool->touchEngine();
    return out;
}

void Panel::chooseInputAlternative(const InputPlace &place, const Alternative &a)
{
    const QTextBlock block = m_input->document()->findBlockByNumber(place.block);
    if (!block.isValid() || block.text().mid(place.start, place.length) != place.sentence)
        return;
    QTextCursor c(block);
    c.setPosition(block.position() + place.start);
    c.setPosition(block.position() + place.start + place.length, QTextCursor::KeepAnchor);
    c.insertText(afterModel(a.sentence, m_tool->rules()));
}

void Panel::showInputAlternatives(int position, const QPoint &globalPos)
{
    const InputPlace place = inputPlace(position);
    if (place.word < 0)
        return;
    QApplication::setOverrideCursor(Qt::BusyCursor);
    setStatus(tr("Looking for other words for “%1”...").arg(place.wordText));
    QString error;
    const QList<Alternative> alts = inputAlternatives(place, &error);
    QApplication::restoreOverrideCursor();
    setStatus(alts.isEmpty() ? (error.isEmpty() ? tr("No other words found.") : tr("No other words: %1").arg(error))
                             : tr("Choose another word; the rest of the sentence is fitted to it."));
    if (alts.isEmpty())
        return;
    QMenu menu(this);
    menu.setObjectName(QStringLiteral("tpInputWordMenu"));
    menu.setToolTipsVisible(true);
    for (const Alternative &a : alts) {
        QAction *ac = menu.addAction(a.word);
        ac->setToolTip(a.sentence);
        connect(ac, &QAction::triggered, this, [this, place, a] { chooseInputAlternative(place, a); });
    }
    menu.exec(globalPos);
}

void Panel::takeFromEditor()
{
    qb::tools::ToolHost *host = m_tool->host();
    const QString text = host ? host->selectedText() : QString();
    if (text.trimmed().isEmpty()) {
        setStatus(tr("Select some text in the editor first."));
        return;
    }
    setInput(text);
    run();
}

void Panel::putIntoEditor()
{
    qb::tools::ToolHost *host = m_tool->host();
    if (m_units.isEmpty()) {
        setStatus(tr("There is no result yet."));
        return;
    }
    if (!host || !host->canInsertText()) {
        setStatus(tr("Click into a text in the editor first (or select the text to be replaced)."));
        return;
    }
    if (host->insertText(resultText()))
        setStatus(tr("Put into the editor. Edit > Undo there takes it back."));
}

void Panel::copyResult()
{
    if (m_units.isEmpty()) {
        setStatus(tr("There is no result yet."));
        return;
    }
    QGuiApplication::clipboard()->setText(resultText());
    setStatus(tr("The result is copied."));
}

} // namespace tp
