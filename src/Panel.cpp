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
#include <QUrl>
#include <QRegularExpression>

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

QList<Alternative> runAltJob(const AltJob &j, QString *error)
{
    TranslateTool::lowerPriority();
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

void InputEdit::mouseDoubleClickEvent(QMouseEvent *e)
{
    QPlainTextEdit::mouseDoubleClickEvent(e);   // the word is selected as usual
    m_panel->showInfoForInput(cursorForPosition(e->pos()).position());
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
                          "another word. Every other word can be clicked for other words, too; a double click shows a "
                          "word's suggestions at the bottom."));
    viewport()->setCursor(Qt::PointingHandCursor);
    connect(this, &QTextBrowser::anchorClicked, this, [this](const QUrl &url) { emit linkClicked(url.toString()); });
    m_clickTimer.setSingleShot(true);
    m_clickTimer.setInterval(QApplication::doubleClickInterval());
    connect(&m_clickTimer, &QTimer::timeout, this, [this] {
        if (m_clickPos >= 0)
            emit wordClicked(m_clickPos, m_clickGlobal);
    });
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
    if (m_clickPos == -2) {   // the release of a double click
        m_clickPos = -1;
        return;
    }
    if (!anchorAt(e->pos()).isEmpty())   // a link: anchorClicked (below) takes it
        return;
    if (e->button() != Qt::LeftButton || (e->pos() - m_pressPos).manhattanLength() > 4 || textCursor().hasSelection())
        return;
    const int pos = charAt(e->pos());
    if (pos >= 0) {
        // the menu after the double-click time: a double click is no click
        m_clickPos = pos;
        m_clickGlobal = e->globalPosition().toPoint();
        m_clickTimer.start();
    }
}

void OutputView::mouseDoubleClickEvent(QMouseEvent *e)
{
    m_clickTimer.stop();
    const int pos = charAt(e->pos());
    m_clickPos = -2;
    if (pos >= 0 && anchorAt(e->pos()).isEmpty())
        emit wordDoubleClicked(pos);
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
    connect(m_output, &OutputView::wordDoubleClicked, this, &Panel::showInfoForOutput);
    connect(m_output, &OutputView::linkClicked, this, &Panel::linkClicked);
    connect(m_output, &OutputView::contextRequested, this, &Panel::outputContextMenu);

    // 1.1: the information area, and live work about a second after the writer stops typing
    m_info = new QTextBrowser(this);
    m_info->setObjectName(QStringLiteral("tpInfo"));
    m_info->setOpenLinks(false);
    m_info->setPlaceholderText(tr("Double-click a word in the text or in the result: its spelling suggestions, words of "
                                  "a similar meaning and the model's other words appear here."));
    m_info->setMinimumHeight(m_info->fontMetrics().lineSpacing() * 3);
    m_live = toolButton(QStringLiteral("view-refresh"), tr("Live"),
                        tr("Live: about a second after you stop typing, the changed sentences are worked on again"), this);
    m_live->setObjectName(QStringLiteral("tpLive"));
    m_live->setCheckable(true);
    m_live->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_live->setChecked(m_tool->setting(QStringLiteral("live"), true).toBool());
    connect(m_live, &QToolButton::toggled, this, [this](bool on) {
        m_tool->setSetting(QStringLiteral("live"), on);
        if (on && !inputText().trimmed().isEmpty())
            m_liveTimer.start();
        else if (!on)
            m_liveTimer.stop();
    });
    m_liveTimer.setSingleShot(true);
    m_liveTimer.setInterval(1000);
    connect(&m_liveTimer, &QTimer::timeout, this, [this] { runNow(true); });
    connect(m_input, &QPlainTextEdit::textChanged, this, &Panel::liveTyped);

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
    buttons->addWidget(m_live);
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
    m_split->addWidget(m_info);
    m_split->setCollapsible(1, false);
    m_split->setStretchFactor(0, 2);
    m_split->setStretchFactor(1, 0);
    m_split->setStretchFactor(2, 3);
    m_split->setStretchFactor(3, 1);
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
    ml->addWidget(m_live, 0, Qt::AlignHCenter);
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

    // the information area under both, the width of the window
    auto *outer = new QSplitter(Qt::Vertical, this);
    outer->setObjectName(QStringLiteral("tpInfoSplit"));
    outer->addWidget(m_split);
    outer->addWidget(m_info);
    outer->setStretchFactor(0, 4);
    outer->setStretchFactor(1, 1);
    auto *l = new QVBoxLayout(this);
    l->setContentsMargins(6, 6, 6, 6);
    l->addLayout(top);
    l->addWidget(outer, 1);
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
    m_liveTimer.stop();   // the caller runs when it wants a result
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
    m_liveTimer.stop();
    runNow(false);
}

QString Panel::systemFor(const QString &what)
{
    if (what == QLatin1String("translate"))
        return QStringLiteral(
            "You translate German fiction into natural, idiomatic American English. Translate the user's text "
            "faithfully: keep its meaning, tone, tense and paragraph breaks, and keep dialogue as dialogue with the "
            "same quotation marks. Do not add, explain or leave out anything. Answer with the translation only.");
    if (what == QLatin1String("formal"))
        return QStringLiteral(
            "You are an editor. Rewrite the user's English text in a more formal tone. Keep its meaning, its facts and "
            "its dialogue as dialogue. Do not explain. Answer with the rewritten text only.");
    if (what == QLatin1String("paraphrase"))
        return QStringLiteral(
            "You are an editor. Repeat the user's English sentence; you may choose other words, but keep its meaning, "
            "tone and length. Answer with the sentence only.");
    return QStringLiteral(
        "You are a careful proofreader of English fiction. Correct only clear errors of spelling, grammar and "
        "punctuation in the user's text, in American English. Keep the author's words, style, tone and sentence "
        "structure. Keep dialogue as written, including informal speech, slang, contractions and sentence fragments. "
        "Do not add, move or remove quotation marks. Do not rephrase, explain or comment. If there is nothing to "
        "correct, repeat the text unchanged. Answer with the text only.");
}

bool Panel::chatModel(const QString &modelId) const
{
    const CatalogModel *m = m_tool->store()->model(modelId);
    return m && m->type == QLatin1String("chat");
}

void Panel::liveTyped()
{
    if (isLive())
        m_liveTimer.start();
}

bool Panel::isLive() const
{
    return m_live && m_live->isChecked();
}

void Panel::setLive(bool on)
{
    m_live->setChecked(on);
}

void Panel::runNow(bool live)
{
    if (m_busy) {
        if (live) {   // typed on while a run was busy: it stops after the sentences being worked on, then runs again
            m_rerun = true;
            if (m_cancel)
                m_cancel->store(true);
        }
        return;
    }
    m_liveRun = live;
    const QString text = inputText();
    if (text.trimmed().isEmpty()) {
        if (live) {
            m_units.clear();
            m_single = SingleWord();
            m_output->clear();
            m_notes->setVisible(false);
            setStatus(tr("Ready."));
        } else {
            setStatus(tr("There is no text to work on yet."));
        }
        emit finished();
        return;
    }
    const QString task = currentTask();
    m_lang = task == QLatin1String("translate") ? QStringLiteral("de")
             : task == QLatin1String("proofread") ? QStringLiteral("en")
                                                  : detectLanguage(text);
    m_styleId = m_style->currentData().toString();
    const QString kind = slot();
    const Rules rules = m_tool->rules();
    // the writer's decisions on the sentences that stay as they were
    QHash<QString, QList<Segment>> previous;
    for (const Unit &u : std::as_const(m_units))
        previous.insert(u.original, u.segments);

    // a single English word: corrected by the word list, with every suggestion (no model needed)
    QString single;
    if (proofreading() && isSingleWord(text, &single)) {
        m_plan = makePlan(text.trimmed(), false);
        m_units.clear();
        m_noteList.clear();
        Unit u;
        u.original = text.trimmed();
        u.modelInput = toPlainQuotes(u.original);
        m_units << u;
        finishSingle();
        return;
    }

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
    // 1.1: never a model the computer has no room for (it would make the system swap or stop programs)
    const QString memory = m_tool->loadProblem(kind, modelId);
    if (!memory.isEmpty()) {
        setStatus(memory);
        emit finished();
        return;
    }
    m_plan = makePlan(text, !proofreading());
    m_units.clear();
    m_noteList.clear();
    m_single = SingleWord();
    int dashes = 0;
    for (const QString &u : std::as_const(m_plan.units)) {
        Unit unit;
        unit.original = u;
        int n = 0;
        unit.modelInput = toPlainQuotes(beforeModel(u, rules, &n));
        dashes += n;
        m_units << unit;
    }
    // keep the decisions for later (finishRun compares)
    for (Unit &u : m_units)
        if (previous.contains(u.original))
            u.segments = previous.value(u.original);
    if (dashes)
        m_noteList << (dashes == 1 ? tr("1 dash replaced by a comma (rule: no em-dashes): click it to keep the dash.")
                                   : tr("%1 dashes replaced (rule: no em-dashes): click one to keep the dash.").arg(dashes));

    RunOptions o;
    const bool chat = chatModel(modelId);
    if (chat) {
        o.system = systemFor(proofreading() ? m_styleId : QStringLiteral("translate"));
        o.batch = 4;
    } else {
        o.prefix = proofreading() ? stylePrefix() : QString();
        o.batch = 8;
    }
    // beam 4: with 2 the large corrector turned "bellow" into "downstairs" (test of 2026-11-15)
    o.beam = m_tool->setting(QStringLiteral("careful"), false).toBool() ? 6 : 4;
    o.banDashes = rules.noEmDash;
    // the sentences worked on before (same model, same instruction) come from the cache
    m_runKey = modelId + QLatin1Char('\x1f') + o.prefix + o.system + QLatin1Char('\x1f') + (o.banDashes ? u'1' : u'0')
               + QString::number(o.beam);
    m_sent.clear();
    QStringList modelTexts;
    for (int i = 0; i < m_units.size(); ++i)
        if (!m_cache.contains(m_runKey + QLatin1Char('\x1f') + m_units.at(i).modelInput)) {
            m_sent << i;
            modelTexts << m_units.at(i).modelInput;
        }
    if (modelTexts.isEmpty()) {
        finishRun({}, QString(), 0.0, false);
        return;
    }
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
        TranslateTool::lowerPriority();
        QElapsedTimer clock;
        clock.start();
        QString error;
        QStringList results;
        RunOptions opts = o;
        opts.cancel = cancel.get();
        if (engine->load(slotName, files, threads, &error)) {
            results = engine->run(slotName, modelTexts, opts,
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
    m_rerun = false;
    m_liveTimer.stop();
    if (m_cancel)
        m_cancel->store(true);
    setStatus(tr("Stopping after the sentences being worked on..."));
}

void Panel::finishRun(const QStringList &sentResults, const QString &error, double seconds, bool stopped)
{
    setBusy(false);
    m_tool->touchEngine();
    if (!error.isEmpty() || sentResults.size() != m_sent.size()) {
        setStatus(tr("The work failed: %1").arg(error.isEmpty() ? tr("no result") : error));
        m_rerun = false;
        emit finished();
        return;
    }
    // the results of the model and those from the cache
    QStringList results;
    for (int i = 0; i < m_units.size(); ++i)
        results << m_cache.value(m_runKey + QLatin1Char('\x1f') + m_units.at(i).modelInput);
    for (int k = 0; k < m_sent.size(); ++k) {
        const QString &r = sentResults.at(k);
        results[m_sent.at(k)] = r;
        if (!r.isEmpty())
            m_cache.insert(m_runKey + QLatin1Char('\x1f') + m_units.at(m_sent.at(k)).modelInput, r);
    }
    if (m_cache.size() > 5000)
        m_cache.clear();
    const Rules rules = m_tool->rules();
    int kept = 0;
    for (int i = 0; i < m_units.size(); ++i) {
        Unit &u = m_units[i];
        const QList<Segment> before = u.segments;   // the writer's decisions of the run before, if any
        QString raw = results.at(i);
        if (raw.isEmpty()) // stopped before this sentence
            raw = u.modelInput;
        u.raw = raw;
        u.flag.clear();
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
        QString shown = afterModel(u.raw, rules, &m_noteList);
        if (proofreading()) {
            // 1.1: the writer's quotation marks and dialogue punctuation where the writer had them (and curly, if
            // the rule says so: from their places, the direction is clear)
            shown = keepDialogue(u.original, shown);
            if (rules.curlyQuotes)
                shown = toCurlyQuotes(toPlainQuotes(shown));
            u.segments = compareWords(u.original, shown);
            // the same suggestions as in the run before: the writer's choices stay
            if (before.size() == u.segments.size()) {
                bool same = true;
                for (int s = 0; s < before.size() && same; ++s)
                    same = before.at(s).before == u.segments.at(s).before && before.at(s).after == u.segments.at(s).after;
                if (same)
                    u.segments = before;
            }
        } else {
            Segment s;
            s.before = s.after = words(tidyPunctuation(shown));
            u.segments = {s};
        }
    }
    if (kept)
        m_noteList << (kept == 1 ? tr("1 sentence kept as you wrote it: the model's version did not look right "
                                      "(point at the sentence to see what it wrote).")
                                 : tr("%1 sentences kept as you wrote them: the model's versions did not look right "
                                      "(point at a sentence to see what it wrote).").arg(kept));
    // a single German word: its translation, and below it the model's other words and similar English words
    QString single;
    if (!proofreading() && isSingleWord(inputText(), &single) && !stopped) {
        m_single = SingleWord();
        m_single.on = true;
        m_single.word = single;
        QString english;
        isSingleWord(unitText(0), &english);
        m_single.result = english.isEmpty() ? unitText(0) : english;
        m_single.similar = similarWords(m_single.result, &m_single.base);
        requestSingleModelWords();
    }
    render();
    const QString done = m_sent.isEmpty() ? tr("Done") : tr("Done in %1 s").arg(seconds, 0, 'f', 1);
    if (stopped)
        setStatus(m_rerun ? tr("Working on your changes...")
                          : tr("Stopped. The sentences done so far are shown; the rest is as you wrote it."));
    else if (proofreading())
        setStatus(changeCount() == 0 ? tr("%1: no changes.").arg(done)
                  : changeCount() == 1 ? tr("%1: 1 change. Click it for the original and other words.").arg(done)
                                       : tr("%1: %2 changes. Click one for the original and other words.")
                                             .arg(done)
                                             .arg(changeCount()));
    else
        setStatus(tr("Translated (%1). Click a word for other words.").arg(m_sent.isEmpty() ? tr("at once")
                                                                                            : tr("%1 s").arg(seconds, 0, 'f', 1)));
    emit finished();
    if (m_rerun) {
        m_rerun = false;
        QTimer::singleShot(0, this, [this] { runNow(true); });
    }
}

void Panel::finishSingle()
{
    // the word as it should be: as typed when the dictionary knows it, else the nearest word of the dictionary
    Unit &u = m_units[0];
    QString word;
    isSingleWord(u.original, &word);
    m_single = SingleWord();
    m_single.on = true;
    m_single.word = word;
    const bool known = knownWord(word);
    m_single.spelling = known ? QStringList() : spellingSuggestions(word, 12);
    m_single.result = known || m_single.spelling.isEmpty() ? word : m_single.spelling.first();
    m_single.similar = similarWords(m_single.result, &m_single.base);
    QString shown = u.original;
    shown.replace(word, m_single.result);
    u.raw = toPlainQuotes(shown);
    u.segments = compareWords(u.original, shown);
    render();
    if (known)
        setStatus(tr("“%1” is spelled correctly. Below: words of a similar meaning.").arg(word));
    else if (m_single.spelling.isEmpty())
        setStatus(tr("“%1” is not in the dictionary, and no word near it is.").arg(word));
    else
        setStatus(tr("“%1” is not in the dictionary: “%2” is the nearest word. Below: the other suggestions.")
                      .arg(word, m_single.result));
    requestSingleModelWords();
    emit finished();
}

void Panel::requestSingleModelWords()
{
    // the model's other words for the word (proofreading: the corrector's; translation: other translations)
    AltJob job;
    QString jobError;
    if (!m_single.on || m_units.isEmpty() || !outputJob(0, 0, &job, &jobError))
        return;
    m_single.modelPending = true;
    const int request = ++m_singleRequest;
    const QString current = words(job.current).value(0);
    QPointer<Panel> self(this);
    QThread *t = QThread::create([self, job, request, current] {
        TranslateTool::lowerPriority();
        QString error;
        const QList<Alternative> found = runAltJob(job, &error);
        QMetaObject::invokeMethod(qApp, [self, found, request, current] {
            if (!self || request != self->m_singleRequest)
                return;
            self->m_tool->touchEngine();
            self->m_single.modelPending = false;
            QStringList words;
            for (const Alternative &a : self->filterAlternatives(found, current))
                words << a.word;
            if (self->proofreading()) {
                // proofreading a word alone: only the model's words close to it in spelling or meaning
                QStringList near = self->m_single.spelling;
                near << self->m_single.word;
                for (const Similar &sim : std::as_const(self->m_single.similar))
                    near << sim.words;
                words = usefulModelWords(words, self->m_single.result, false, &near);
            } else {
                words = usefulModelWords(words, self->m_single.result);
            }
            for (const QString &w : std::as_const(words))
                if (w.compare(self->m_single.word, Qt::CaseInsensitive) != 0 && !self->m_single.modelWords.contains(w))
                    self->m_single.modelWords << w;
            self->render();
        }, Qt::QueuedConnection);
    });
    connect(t, &QThread::finished, t, &QObject::deleteLater);
    t->start();
}

void Panel::linkClicked(const QString &href)
{
    // a word of the suggestions under a single word: it becomes the result
    if (!href.startsWith(QLatin1String("tp-word:")) || m_units.isEmpty())
        return;
    const QString w = QUrl::fromPercentEncoding(href.mid(8).toUtf8());
    QString shown = m_units.at(0).original;
    shown.replace(m_single.word, w);
    chooseOutputAlternative(0, Alternative{w, toPlainQuotes(shown)});
    m_single.result = w;
    render();
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
    if (m_single.on) {
        // 1.1: under a single word, every suggestion for it; a click on one makes it the result
        QTextCharFormat head = plain;
        head.setForeground(pal.color(QPalette::PlaceholderText));
        QFont f = m_output->font();
        f.setPointSizeF(f.pointSizeF() * 0.9);
        head.setFont(f);
        QTextCharFormat link = plain;
        link.setAnchor(true);
        link.setForeground(pal.color(QPalette::Link));
        auto list = [&](const QString &title, const QStringList &items) {
            if (items.isEmpty())
                return;
            c.insertBlock();
            c.insertText(title + QStringLiteral(" "), head);
            for (int i = 0; i < items.size(); ++i) {
                if (i > 0)
                    c.insertText(QStringLiteral(", "), plain);
                QTextCharFormat l = link;
                l.setAnchorHref(QStringLiteral("tp-word:") + QString::fromUtf8(QUrl::toPercentEncoding(items.at(i))));
                l.setToolTip(tr("Use “%1”").arg(items.at(i)));
                c.insertText(items.at(i), l);
            }
        };
        c.insertBlock();
        if (proofreading())
            list(tr("Spelling:"), m_single.spelling);
        for (const Similar &sim : std::as_const(m_single.similar))
            list(tr("Similar (%1%2):").arg(sim.pos, m_single.base.isEmpty() || m_single.base == m_single.result.toLower()
                                                        ? QString()
                                                        : tr(", for “%1”").arg(m_single.base)),
                 sim.words);
        if (!m_single.modelWords.isEmpty())
            list(proofreading() ? tr("The model’s other words:") : tr("Other translations:"), m_single.modelWords);
        else if (m_single.modelPending) {
            c.insertBlock();
            c.insertText(tr("Asking the model for other words..."), head);
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
    const QSet<QString> &list = wordList();
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
    if (const QString memory = m_tool->loadProblem(slot(), modelId); !memory.isEmpty()) {
        if (error)
            *error = memory;
        return false;
    }
    job->slot = slot();
    job->files = m_tool->store()->files(modelId);
    job->threads = m_tool->threads();
    job->source = m_units.at(unit).modelInput;
    job->current = toPlainQuotes(unitText(unit));
    job->word = word;
    if (chatModel(modelId))
        job->options.system = systemFor(proofreading() ? m_styleId : QStringLiteral("translate"));
    else
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
    QString shown = afterModel(a.sentence, m_tool->rules(), &notes);
    if (proofreading()) {
        shown = keepDialogue(u.original, shown);
        if (m_tool->rules().curlyQuotes)
            shown = toCurlyQuotes(toPlainQuotes(shown));
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
    if (const QString memory = m_tool->loadProblem(QStringLiteral("corrector"), modelId); !memory.isEmpty()) {
        if (error)
            *error = memory;
        return {};
    }
    job.slot = QStringLiteral("corrector");
    job.files = m_tool->store()->files(modelId);
    job.threads = m_tool->threads();
    job.source = job.current = toPlainQuotes(place.sentence);
    job.word = place.word;
    if (chatModel(modelId))
        job.options.system = systemFor(QStringLiteral("paraphrase"));
    else
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

// ---------------------------------------------------------------- the information area (1.1)

void Panel::showInfoForOutput(int position)
{
    const WordInfo w = wordAt(position);
    if (w.unit < 0 || w.word < 0)
        return;
    QString core;
    if (!isSingleWord(w.text, &core))
        core = w.text;
    m_infoWord = core;
    m_infoContext = QStringLiteral("output");
    m_infoEnglish = true;   // the result is English (proofread or translated)
    m_infoModelWords.clear();
    m_infoModelError.clear();
    AltJob job;
    QString jobError;
    const bool ok = !m_busy && outputJob(w.unit, w.word, &job, &jobError);
    requestInfoModelWords(job, ok, jobError, w.text);
}

void Panel::showInfoForInput(int position)
{
    const InputPlace place = inputPlace(position);
    const QTextBlock block = m_input->document()->findBlock(position);
    // the word under the position, also in German text (inputPlace gives English words only)
    QString word = place.wordText;
    if (word.isEmpty() && block.isValid()) {
        const QString text = block.text();
        int a = position - block.position(), b = a;
        auto inWord = [](QChar c) { return c.isLetter() || c == QLatin1Char('\'') || c == QChar(0x2019) || c == QLatin1Char('-'); };
        while (a > 0 && inWord(text.at(a - 1)))
            --a;
        while (b < text.size() && inWord(text.at(b)))
            ++b;
        word = text.mid(a, b - a);
    }
    QString core;
    if (!isSingleWord(word, &core))
        return;
    m_infoWord = core;
    m_infoContext = QStringLiteral("input");
    m_infoEnglish = block.isValid() && detectLanguage(block.text()) != QLatin1String("de");
    m_infoModelWords.clear();
    m_infoModelError.clear();
    // the model's other words for an English word of the input: those the paraphrasing model would choose
    AltJob job;
    QString jobError;
    bool ok = false;
    if (m_infoEnglish && place.word >= 0 && !m_busy) {
        const QString modelId = m_tool->chosenModel(QStringLiteral("corrector"));
        job.engine = modelId.isEmpty() ? nullptr : m_tool->engine(&jobError);
        jobError = modelId.isEmpty() ? tr("no proofreading model downloaded") : jobError;
        if (job.engine) {
            jobError = m_tool->loadProblem(QStringLiteral("corrector"), modelId);
            ok = jobError.isEmpty();
            job.slot = QStringLiteral("corrector");
            job.files = m_tool->store()->files(modelId);
            job.threads = m_tool->threads();
            job.source = job.current = toPlainQuotes(place.sentence);
            job.word = place.word;
            if (chatModel(modelId))
                job.options.system = systemFor(QStringLiteral("paraphrase"));
            else
                job.options.prefix = kParaphrase;
            job.options.banDashes = m_tool->rules().noEmDash;
        }
    }
    requestInfoModelWords(job, ok, jobError, place.wordText);
}

void Panel::requestInfoModelWords(const AltJob &job, bool ok, const QString &jobError, const QString &word)
{
    const int request = ++m_infoRequest;
    m_infoPending = ok;
    m_infoModelError = ok ? QString() : jobError;
    fillInfo();
    if (!ok)
        return;
    QPointer<Panel> self(this);
    QThread *t = QThread::create([self, job, request, word] {
        QString error;
        const QList<Alternative> found = runAltJob(job, &error);
        QMetaObject::invokeMethod(qApp, [self, found, error, request, word] {
            if (!self || request != self->m_infoRequest)
                return;
            self->m_tool->touchEngine();
            self->m_infoPending = false;
            self->m_infoModelError = error;
            QStringList words;
            for (const Alternative &a : self->filterAlternatives(found, word))
                words << a.word;
            // proofreading: the word's other forms are no other words ("windows" for "window")
            for (const QString &w : usefulModelWords(words, word, !self->proofreading()))
                if (!self->m_infoModelWords.contains(w))
                    self->m_infoModelWords << w;
            self->fillInfo();
        }, Qt::QueuedConnection);
    });
    connect(t, &QThread::finished, t, &QObject::deleteLater);
    t->start();
}

void Panel::fillInfo()
{
    const QString w = m_infoWord;
    QString html = QStringLiteral("<p><b>%1</b>").arg(w.toHtmlEscaped());
    if (!m_infoEnglish) {
        html += tr(" (German): spelling suggestions and similar words are given for English words.") + QStringLiteral("</p>");
        m_info->setHtml(html);
        return;
    }
    QString base;
    const QList<Similar> similar = similarWords(w, &base);
    if (!base.isEmpty() && base != w.toLower())
        html += tr(" (from “%1”)").arg(base.toHtmlEscaped());
    html += QStringLiteral("</p>");
    if (knownWord(w)) {
        html += QStringLiteral("<p>") + tr("Spelling: correct.") + QStringLiteral("</p>");
    } else {
        const QStringList sp = spellingSuggestions(w, 10);
        html += QStringLiteral("<p>") + (sp.isEmpty() ? tr("Spelling: not in the dictionary, and no word near it is.")
                                                      : tr("Spelling: not in the dictionary. Did you mean %1?")
                                                            .arg(sp.join(QStringLiteral(", ")).toHtmlEscaped()))
                + QStringLiteral("</p>");
    }
    if (similar.isEmpty())
        html += QStringLiteral("<p>") + tr("Similar words: none known.") + QStringLiteral("</p>");
    for (const Similar &s : similar)
        html += QStringLiteral("<p>") + tr("Similar (%1): %2").arg(s.pos, s.words.join(QStringLiteral(", ")).toHtmlEscaped())
                + QStringLiteral("</p>");
    if (m_infoPending)
        html += QStringLiteral("<p><i>") + tr("Asking the model for other words...") + QStringLiteral("</i></p>");
    else if (!m_infoModelWords.isEmpty())
        html += QStringLiteral("<p>") + tr("The model's other words here: %1").arg(m_infoModelWords.join(QStringLiteral(", ")).toHtmlEscaped())
                + QStringLiteral("</p>");
    else if (!m_infoModelError.isEmpty())
        html += QStringLiteral("<p><i>") + tr("The model's other words: %1").arg(m_infoModelError.toHtmlEscaped()) + QStringLiteral("</i></p>");
    m_info->setHtml(html);
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
