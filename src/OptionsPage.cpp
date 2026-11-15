#include "OptionsPage.h"

#include "TranslateTool.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QThread>
#include <QVBoxLayout>

namespace tp {

OptionsPage::OptionsPage(TranslateTool *tool, QWidget *parent)
    : m_tool(tool)
{
    m_w = new QWidget(parent);
    m_w->setObjectName(QStringLiteral("tpOptions"));
    auto *l = new QVBoxLayout(m_w);
    auto *intro = new QLabel(tr("English text is proofread and German text is translated into English, on this computer. "
                                "Choose a model for each and download it once (it is checked, then used without any "
                                "connection)."),
                             m_w);
    intro->setWordWrap(true);
    l->addWidget(intro);
    const Machine machine = Machine::now();
    auto *computer = new QLabel(machine.totalMB > 0
                                    ? tr("This computer has %1 GB of memory, %2 GB free now. A model that needs more "
                                         "than it can give is not offered for download, and is not loaded when too "
                                         "little memory is free.")
                                          .arg(machine.totalMB / 1024.0, 0, 'f', 1)
                                          .arg(machine.availableMB / 1024.0, 0, 'f', 1)
                                    : QString(),
                                m_w);
    computer->setObjectName(QStringLiteral("tpMachine"));
    computer->setWordWrap(true);
    computer->setVisible(machine.totalMB > 0);
    l->addWidget(computer);
    l->addWidget(modelGroup(QStringLiteral("corrector"), tr("Proofreading (English)"),
                            tr("Try them: CoEdIT Large finds many errors but rewrites more; the Qwen models are told to "
                               "fix only real errors and to leave your style and dialogue alone (4B is better, 1.7B "
                               "faster). A Qwen model downloaded once also translates.")));
    l->addWidget(modelGroup(QStringLiteral("translator"), tr("Translation (German to English)"), QString()));

    auto *rules = new QGroupBox(tr("Rules for the result"), m_w);
    auto *rl = new QVBoxLayout(rules);
    m_noDash = new QCheckBox(tr("No em-dashes (a dash between words becomes a comma; the models may not write one)"), rules);
    m_american = new QCheckBox(tr("American spelling (colour → color, realise → realize, towards → toward ...)"), rules);
    m_curly = new QCheckBox(tr("Curly quotes and apostrophes (“don’t”)"), rules);
    m_ellipsis = new QCheckBox(tr("Three periods (...) instead of the ellipsis character"), rules);
    for (QCheckBox *c : {m_noDash, m_american, m_curly, m_ellipsis}) {
        c->setChecked(true);
        rl->addWidget(c);
    }
    auto *own = new QLabel(tr("Your own replacements, one per line, as <i>old =&gt; new</i> (whole words; the case of "
                              "the first letter is kept):"),
                           rules);
    own->setWordWrap(true);
    rl->addWidget(own);
    m_replacements = new QPlainTextEdit(rules);
    m_replacements->setObjectName(QStringLiteral("tpReplacements"));
    m_replacements->setPlaceholderText(QStringLiteral("OK => okay\nalright => all right"));
    m_replacements->setMaximumHeight(90);
    rl->addWidget(m_replacements);
    m_replacementErrors = new QLabel(rules);
    m_replacementErrors->setVisible(false);
    rl->addWidget(m_replacementErrors);
    connect(m_replacements, &QPlainTextEdit::textChanged, this, [this] {
        QStringList errors;
        parseReplacements(m_replacements->toPlainText(), &errors);
        m_replacementErrors->setText(errors.join(QLatin1Char('\n')));
        m_replacementErrors->setVisible(!errors.isEmpty());
    });
    l->addWidget(rules);

    auto *speed = new QGroupBox(tr("Speed"), m_w);
    auto *sf = new QFormLayout(speed);
    m_threads = new QSpinBox(speed);
    m_threads->setRange(1, qMax(1, QThread::idealThreadCount()));
    m_threads->setToolTip(tr("One core is left free by default, so that the computer stays responsive; the work runs "
                             "at a low priority."));
    sf->addRow(tr("Processor threads:"), m_threads);
    m_careful = new QCheckBox(tr("Careful: the model weighs even more possibilities (slower, now and then better)"), speed);
    sf->addRow(m_careful);
    l->addWidget(speed);

    QStringList credits;
    for (const CatalogModel &m : m_tool->store()->models())
        credits << QStringLiteral("<li>%1</li>").arg(m.credit.toHtmlEscaped());
    auto *about = new QLabel(tr("<b>Models</b><ul>%1</ul>The engine: CTranslate2 (MIT), oneDNN (Apache-2.0), "
                                "SentencePiece (Apache-2.0); the American spellings from SCOWL by Kevin Atkinson; the "
                                "words of a similar meaning from Princeton WordNet 3.0. Details in the add-on's licenses "
                                "folder. Add-on %2.")
                                 .arg(credits.join(QString()), m_tool->version()),
                             m_w);
    about->setWordWrap(true);
    about->setTextFormat(Qt::RichText);
    about->setOpenExternalLinks(true);
    l->addWidget(about);
    l->addStretch(1);

    // the settings
    m_noDash->setChecked(m_tool->setting(QStringLiteral("noEmDash"), true).toBool());
    m_american->setChecked(m_tool->setting(QStringLiteral("american"), true).toBool());
    m_curly->setChecked(m_tool->setting(QStringLiteral("curlyQuotes"), true).toBool());
    m_ellipsis->setChecked(m_tool->setting(QStringLiteral("plainEllipsis"), true).toBool());
    m_replacements->setPlainText(m_tool->setting(QStringLiteral("replacements")).toString());
    m_threads->setValue(m_tool->threads());
    m_careful->setChecked(m_tool->setting(QStringLiteral("careful"), false).toBool());

    connect(m_tool->store(), &ModelStore::progress, this, [this](const QString &id, qint64 done, qint64 total) {
        for (const Row &r : m_rows.values(id)) {
            r.bar->setVisible(true);
            r.bar->setRange(0, 1000);
            r.bar->setValue(total > 0 ? int(1000 * done / total) : 0);
            r.state->setText(tr("Downloading: %1 of %2 MB").arg(done / 1000000).arg(total / 1000000));
        }
    });
    connect(m_tool->store(), &ModelStore::finished, this, [this](const QString &id, bool ok, const QString &message) {
        refresh();
        if (!ok)
            for (const Row &r : m_rows.values(id))
                r.state->setText(message);
    });
    refresh();
}

QWidget *OptionsPage::modelGroup(const QString &kind, const QString &title, const QString &intro)
{
    auto *box = new QGroupBox(title, m_w);
    auto *g = new QGridLayout(box);
    int row = 0;
    if (!intro.isEmpty()) {
        auto *i = new QLabel(intro, box);
        i->setWordWrap(true);
        g->addWidget(i, row++, 0, 1, 4);
    }
    auto *group = new QButtonGroup(box);
    m_groups.insert(kind, group);
    const QString chosen = m_tool->setting(kind).toString();
    for (const CatalogModel &m : m_tool->store()->models(kind)) {
        Row r;
        r.choose = new QRadioButton(m.title, box);
        r.choose->setObjectName(QStringLiteral("tpChoose:") + m.id);
        r.choose->setProperty("modelId", m.id);
        group->addButton(r.choose);
        r.choose->setChecked(m.id == chosen);
        auto *info = new QLabel(tr("%1<br>%2 MB to download, about %3 MB of memory while working. License: "
                                   "<a href=\"%4\">%5</a>%6.")
                                    .arg(m.summary.toHtmlEscaped())
                                    .arg(m.sizeMB)
                                    .arg(m.memoryMB)
                                    .arg(m.licenseUrl.toHtmlEscaped(), m.license.toHtmlEscaped(),
                                         m.nonCommercial ? tr(" (non-commercial use only)") : QString()),
                                box);
        info->setWordWrap(true);
        info->setTextFormat(Qt::RichText);
        info->setOpenExternalLinks(true);
        info->setContentsMargins(22, 0, 0, 0);
        r.state = new QLabel(box);
        r.state->setObjectName(QStringLiteral("tpState:") + m.id);
        r.state->setContentsMargins(22, 0, 0, 0);
        r.bar = new QProgressBar(box);
        r.bar->setVisible(false);
        r.bar->setTextVisible(false);
        r.bar->setMaximumHeight(8);
        r.download = new QPushButton(tr("Download"), box);
        r.download->setObjectName(QStringLiteral("tpDownload:") + m.id);
        r.remove = new QPushButton(tr("Remove"), box);
        r.folder = new QPushButton(tr("From Folder..."), box);
        r.folder->setToolTip(tr("Use the model's files from a folder (for a computer without internet): the files of "
                                "the release \"%1\" of the add-on's repository.")
                                 .arg(m.release));
        const QString id = m.id;
        QRadioButton *choose = r.choose;
        connect(r.download, &QPushButton::clicked, this, [this, id, choose] {
            if (m_tool->store()->busyWith() == id) {
                m_tool->store()->cancel();
            } else {
                startDownload(id);
                if (m_tool->store()->busyWith() == id)
                    choose->setChecked(true);   // chosen in the group where the writer clicked
            }
        });
        connect(r.remove, &QPushButton::clicked, this, [this, id] {
            QString error;
            if (!m_tool->store()->remove(id, &error))
                QMessageBox::warning(m_w, m_tool->name(), error);
            refresh();
        });
        connect(r.folder, &QPushButton::clicked, this, [this, id] { fromFolder(id); });
        g->addWidget(r.choose, row, 0, 1, 1);
        g->addWidget(r.download, row, 1);
        g->addWidget(r.folder, row, 2);
        g->addWidget(r.remove, row, 3);
        ++row;
        g->addWidget(info, row++, 0, 1, 4);
        g->addWidget(r.bar, row++, 0, 1, 4);
        g->addWidget(r.state, row++, 0, 1, 4);
        m_rows.insert(m.id, r);
    }
    g->setColumnStretch(0, 1);
    return box;
}

void OptionsPage::refresh()
{
    ModelStore *store = m_tool->store();
    for (auto it = m_rows.begin(); it != m_rows.end(); ++it) {
        const Row &r = it.value();
        const bool installed = store->isInstalled(it.key());
        const bool loading = store->busyWith() == it.key();
        // 1.1: a model too large for this computer (or for the free disk space) is not offered
        const QString problem = installed || loading ? QString() : store->downloadProblem(it.key());
        r.state->setText(loading ? tr("Downloading ...")
                         : installed ? tr("Downloaded and checked.")
                         : !problem.isEmpty() ? problem
                                              : tr("Not downloaded."));
        r.bar->setVisible(loading);
        r.download->setText(loading ? tr("Stop") : tr("Download"));
        r.download->setEnabled(!installed && problem.isEmpty() && (loading || !store->busy()));
        r.remove->setEnabled(installed && !loading);
        r.folder->setEnabled(!installed && problem.isEmpty() && !store->busy());
    }
    // a kind without a choice yet: the first installed model
    for (auto it = m_groups.begin(); it != m_groups.end(); ++it) {
        if (it.value()->checkedButton())
            continue;
        for (QAbstractButton *b : it.value()->buttons())
            if (store->isInstalled(b->property("modelId").toString())) {
                b->setChecked(true);
                break;
            }
    }
}

void OptionsPage::startDownload(const QString &id)
{
    const CatalogModel *m = m_tool->store()->model(id);
    if (!m)
        return;
    const QString key = QStringLiteral("accepted/") + id;
    if (m->nonCommercial && !m_tool->setting(key, false).toBool()) {
        QMessageBox box(QMessageBox::Question, m_tool->name(),
                        tr("%1 may be used for non-commercial purposes only (license %2).").arg(m->title, m->license),
                        QMessageBox::Cancel, m_w);
        box.setInformativeText(tr("%1\n\nBy downloading it you accept its license: %2").arg(m->credit, m->licenseUrl));
        QPushButton *accept = box.addButton(tr("Accept and Download"), QMessageBox::AcceptRole);
        box.setDefaultButton(QMessageBox::Cancel);
#ifdef TP_TEST_SWITCHES
        const bool ask = !qEnvironmentVariableIsSet("QB_TP_ACCEPT_LICENSES"); // test builds only
#else
        const bool ask = true;
#endif
        if (ask) {
            box.exec();
            if (box.clickedButton() != accept)
                return;
        }
        m_tool->setSetting(key, true);
    }
    m_tool->store()->download(id);
    refresh();
    // a group without a choice yet takes it (a chat model is in both)
    for (const Row &r : m_rows.values(id))
        if (r.choose && r.choose->group() && !r.choose->group()->checkedButton())
            r.choose->setChecked(true);
}

void OptionsPage::fromFolder(const QString &id)
{
    const QString dir = QFileDialog::getExistingDirectory(m_w, tr("Folder with the model's files"));
    if (dir.isEmpty())
        return;
    QString error;
    if (!m_tool->store()->importFolder(id, dir, &error))
        QMessageBox::warning(m_w, m_tool->name(), tr("The model could not be taken from the folder: %1").arg(error));
    refresh();
}

void OptionsPage::apply()
{
    for (auto it = m_groups.begin(); it != m_groups.end(); ++it)
        if (QAbstractButton *b = it.value()->checkedButton())
            m_tool->setSetting(it.key(), b->property("modelId").toString());
    m_tool->setSetting(QStringLiteral("noEmDash"), m_noDash->isChecked());
    m_tool->setSetting(QStringLiteral("american"), m_american->isChecked());
    m_tool->setSetting(QStringLiteral("curlyQuotes"), m_curly->isChecked());
    m_tool->setSetting(QStringLiteral("plainEllipsis"), m_ellipsis->isChecked());
    m_tool->setSetting(QStringLiteral("replacements"), m_replacements->toPlainText());
    m_tool->setSetting(QStringLiteral("threads"), m_threads->value());
    m_tool->setSetting(QStringLiteral("careful"), m_careful->isChecked());
    m_tool->settingsApplied();
}

} // namespace tp
