#pragma once
// The models the writer can choose (the catalog built into the add-on), which of them are on this computer, and
// their download: file by file from the release named in the catalog, each file checked against its size and
// SHA-256 sum before the model counts as installed. Interrupted downloads go on where they stopped.
#include "EngineApi.h"

#include <QFile>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;

namespace tp {

struct CatalogFile {
    QString name;     // in the model's folder
    QString asset;    // on the release
    qint64 size = 0;
    QString sha256;
};

struct CatalogModel {
    QString id, kind, title, summary, license, licenseUrl, credit, source;
    bool nonCommercial = false;
    int memoryMB = 0, sizeMB = 0;
    QString tokenizer, spm, spmSource, spmTarget;
    QList<CatalogFile> files;
};

class ModelStore : public QObject
{
    Q_OBJECT
public:
    explicit ModelStore(QObject *parent = nullptr);

    // dataDir: the tool's own folder; the models go into <dataDir>/models/<id>
    void setDataDir(const QString &dir) { m_dataDir = dir; }
    QString modelsDir() const;
    // The folder with Qt's TLS plugin (the add-on brings it: the program has none), added before the first download.
    void setPluginDir(const QString &dir) { m_pluginDir = dir; }

    QList<CatalogModel> models(const QString &kind = QString()) const;
    const CatalogModel *model(const QString &id) const;
    QString release() const { return m_release; }

    bool isInstalled(const QString &id) const;
    ModelFiles files(const QString &id) const;   // for the engine (only for an installed model)
    bool remove(const QString &id, QString *error = nullptr);

    // Downloads every file of the model that is not there yet; one download at a time.
    void download(const QString &id);
    void cancel();
    bool busy() const { return !m_job.isEmpty(); }
    QString busyWith() const { return m_job; }
    // Takes the model's files from a folder (names as in the model folder or as on the release); checks them.
    bool importFolder(const QString &id, const QString &folder, QString *error);

    // Where the files come from: the release of this add-on's repository, or QB_TP_MODEL_SOURCE (tests: a folder or
    // an address that ends with "/").
    QString fileUrl(const CatalogFile &f) const;

signals:
    void progress(const QString &id, qint64 done, qint64 total);
    void finished(const QString &id, bool ok, const QString &message);
    void changed();   // a model was installed or removed

private:
    void loadCatalog();
    void nextFile();
    void finish(bool ok, const QString &message);
    bool checkFile(const QString &path, const CatalogFile &f, QString *error) const;
    QString markerFile(const QString &id) const;

    QString m_dataDir, m_pluginDir, m_release;
    QList<CatalogModel> m_models;
    QNetworkAccessManager *m_net = nullptr;
    QPointer<QNetworkReply> m_reply;
    QFile *m_file = nullptr;
    QString m_job;             // the model being downloaded
    int m_fileIndex = 0;
    qint64 m_doneBefore = 0;   // bytes of the files already finished
    qint64 m_total = 0;
    bool m_cancelled = false;
};

} // namespace tp
