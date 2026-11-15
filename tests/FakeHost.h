#pragma once
// A stand-in for Quillbinder in the checks: it implements the tool interface's ToolHost (1.1) with a main window
// that holds one text editor, settings in memory, and records of what the tool asked for.
#include "ToolApi.h"

#include <QDir>
#include <QHash>
#include <QMainWindow>
#include <QPair>
#include <QPlainTextEdit>
#include <QVariant>

class FakeEvents : public QObject
{
    Q_OBJECT
signals:
    void projectChanged();
    void settingsChanged();
};

class FakeHost : public qb::tools::ToolHost
{
public:
    FakeHost(const QString &addonDir, const QString &dataRoot)
        : m_addonDir(addonDir)
        , m_dataRoot(dataRoot)
    {
        main = new QMainWindow;
        main->setObjectName(QStringLiteral("fakeMainWindow"));
        editor = new QPlainTextEdit(main);
        main->setCentralWidget(editor);
        main->resize(900, 600);
    }
    ~FakeHost() override { delete main; }

    QMainWindow *main;
    QPlainTextEdit *editor;
    mutable FakeEvents events_;
    QHash<QString, QVariant> settings;
    QStringList shownTabs, shownOptions;
    QHash<QString, QPair<bool, QString>> states;   // command -> (checked, icon)

    int apiVersion() const override { return 110; }
    QString appName() const override { return QStringLiteral("Quillbinder"); }
    QString appVersion() const override { return QStringLiteral("0.91.1"); }
    QWidget *window() const override { return main; }
    QStringList contentDirs(const QString &kind) const override
    {
        return kind == QLatin1String("translation-proofreading") ? QStringList{m_addonDir} : QStringList();
    }
    QString dataDir(const QString &toolId) const override
    {
        const QString d = m_dataRoot + QStringLiteral("/tools/") + toolId;
        QDir().mkpath(d);
        return d;
    }
    QVariant setting(const QString &toolId, const QString &key, const QVariant &def) const override
    {
        return settings.value(toolId + QLatin1Char('/') + key, def);
    }
    void setSetting(const QString &toolId, const QString &key, const QVariant &value) override
    {
        settings.insert(toolId + QLatin1Char('/') + key, value);
    }
    bool hasProject() const override { return true; }
    QString projectTitle() const override { return QStringLiteral("Test project"); }
    bool canInsertText() const override { return true; }
    bool insertText(const QString &text) override
    {
        editor->textCursor().insertText(text);
        return true;
    }
    QString selectedText() const override
    {
        return editor->textCursor().selectedText().replace(QChar(0x2029), QLatin1Char('\n'));
    }
    QList<qb::tools::TextDocument> documents(qb::tools::Scope) const override { return {}; }
    int countWords(const QString &text) const override { return text.split(QLatin1Char(' '), Qt::SkipEmptyParts).size(); }
    void showDocument(const QString &) override {}
    void searchProject(const QString &) override {}
    QList<qb::tools::ProjectInfo> openProjects() override { return {}; }
    bool sendToProject(const QString &, const qb::tools::SendRequest &, QString *) override { return false; }
    QString commandLine(const QString &) const override { return QString(); }
    QObject *events() const override { return &events_; }
    void showInspectorTab(const QString &key) override { shownTabs << key; }
    void setCommandState(const QString &commandKey, bool checked, const QString &icon, const QString &) override
    {
        states.insert(commandKey, {checked, icon});
    }
    void showOptions(const QString &toolId) override { shownOptions << toolId; }

private:
    QString m_addonDir, m_dataRoot;
};
