#pragma once
// The add-on's section in File > Options: which models to use (and their download), the writer's rules, speed.
#include "ToolApi.h"

#include <QHash>
#include <QObject>
#include <QPointer>

class QButtonGroup;
class QCheckBox;
class QLabel;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QRadioButton;
class QSpinBox;
class QWidget;

namespace tp {

class TranslateTool;

class OptionsPage : public QObject, public qb::tools::OptionsSection
{
    Q_OBJECT
public:
    OptionsPage(TranslateTool *tool, QWidget *parent);
    QWidget *widget() override { return m_w; }
    void apply() override;

    // for the tests
    void download(const QString &id) { startDownload(id); }

private:
    struct Row {
        QRadioButton *choose = nullptr;
        QLabel *state = nullptr;
        QProgressBar *bar = nullptr;
        QPushButton *download = nullptr, *remove = nullptr, *folder = nullptr;
    };
    QWidget *modelGroup(const QString &kind, const QString &title, const QString &intro);
    void refresh();
    void startDownload(const QString &id);
    void fromFolder(const QString &id);

    TranslateTool *m_tool;
    QPointer<QWidget> m_w;
    QMultiHash<QString, Row> m_rows;   // by model id (1.1: a chat model has a row in both groups)
    QHash<QString, QButtonGroup *> m_groups;
    QCheckBox *m_noDash = nullptr, *m_american = nullptr, *m_curly = nullptr, *m_ellipsis = nullptr, *m_careful = nullptr;
    QPlainTextEdit *m_replacements = nullptr;
    QLabel *m_replacementErrors = nullptr;
    QSpinBox *m_threads = nullptr;
};

} // namespace tp
