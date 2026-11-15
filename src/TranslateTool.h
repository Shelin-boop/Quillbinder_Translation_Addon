#pragma once
// The writing tool "Translation and Proofreading" (tool interface 1.1 of Quillbinder): a panel in the inspector
// (input above, controls between, output below) and the same panel in a window of its own (input left, output
// right), a menu entry and a toolbar button, and a section in File > Options where the writer chooses and downloads
// the models.
#include "EngineApi.h"
#include "Logic.h"
#include "ModelStore.h"
#include "ToolApi.h"

#include <QList>
#include <QObject>
#include <QPointer>
#include <QTimer>

class QMenu;
class QWidget;

namespace tp {

class Panel;

class TranslateTool : public QObject, public qb::tools::ToolInterface, public qb::tools::ToolExtension
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID QB_TOOL_IID)
    Q_INTERFACES(qb::tools::ToolInterface qb::tools::ToolExtension)
public:
    TranslateTool();
    ~TranslateTool() override;

    // ToolInterface 1.0
    QString id() const override { return QStringLiteral("translation-proofreading"); }
    QString name() const override { return tr("Translation and Proofreading"); }
    QString version() const override;
    QList<qb::tools::Command> commands() const override;
    void setHost(qb::tools::ToolHost *host) override;
    void run(const QString &commandKey) override;
    QString optionsPage() const override;
    qb::tools::OptionsSection *createOptions(QWidget *parent) override;
    void shutdown() override;
    int openWindows() const override;

    // ToolExtension 1.1
    QList<qb::tools::InspectorTab> inspectorTabs() const override;
    QWidget *createInspectorTab(const QString &key, QWidget *parent) override;
    QList<qb::tools::ToolbarButton> toolbarButtons() const override;
    QMenu *commandMenu(const QString &commandKey) override;
    // short: it stands under an icon in the row of File > Options (the full name is its tool tip)
    QString optionsTitle() const override { return tr("Proofreading"); }
    QString optionsIcon() const override { return iconPath(); }

    // ---- for the panel and the options
    qb::tools::ToolHost *host() const { return m_host; }
    ModelStore *store() { return &m_store; }
    QVariant setting(const QString &key, const QVariant &def = QVariant()) const;
    void setSetting(const QString &key, const QVariant &value);
    Rules rules() const;
    int threads() const;
    QString chosenModel(const QString &kind) const;   // the chosen model of the kind, if it is installed
    // The engine (loaded on first use); null with *error when it cannot be loaded.
    EngineInterface *engine(QString *error);
    void touchEngine();            // after work: the models are freed after a while without work
    QString addonDir() const;      // the add-on's folder "translation-proofreading"
    QString iconPath() const;
    void showOptions();
    void settingsApplied();        // File > Options was applied: panels take the new settings

    void updateCommandState();     // the add-on's icon and the window's on/off state on its menu entries

    static const QString kWindow, kInspector, kTab;

signals:
    void settingsChangedSignal();

private:
    void openWindow();

    qb::tools::ToolHost *m_host = nullptr;
    ModelStore m_store;
    EngineInterface *m_engine = nullptr;
    QString m_engineError;
    QTimer m_unloadTimer;
    QList<QPointer<Panel>> m_panels;
    QPointer<QWidget> m_window;
    QPointer<QMenu> m_menu;
};

} // namespace tp
