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
    QString asset;    // on the release (empty for a file in parts)
    qint64 size = 0;
    QString sha256;
    QList<CatalogFile> parts;   // 1.1: a file larger than a release takes comes in parts, joined after the download
};

struct CatalogModel {
    QString id, title, summary, license, licenseUrl, credit, source;
    QString release;             // the release of the add-on's repository the files are on ("models-1", "models-2")
    QStringList kinds;           // "corrector", "translator" (a chat model does both)
    QString type;                // "seq2seq" or "chat"
    bool nonCommercial = false;
    int memoryMB = 0, sizeMB = 0;
    QString tokenizer, spm, spmSource, spmTarget, sourceToken;
    bool thinkTags = false;
    QList<CatalogFile> files;
    bool is(const QString &kind) const { return kinds.contains(kind); }
};

// 1.1: what the computer has, so that a model too large for it is never downloaded or loaded (a model that does not
// fit makes the system swap or stop programs).
struct Machine {
    qint64 totalMB = 0;       // memory of the computer
    qint64 availableMB = 0;   // free for programs now (MemAvailable)
    static Machine now();     // /proc/meminfo; TP_TEST_MEMINFO_MB="total,available" in the checks
};
// Why a model does not fit, or an empty text: never more than 75 % of the memory, and not more than is free now
// beside 1 GB for the system (loadedMB: the memory of the models that would be freed first).
QString memoryProblem(const CatalogModel &m, const Machine &machine, bool loadingNow, qint64 loadedMB = 0);

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
    QString fileUrl(const CatalogModel &m, const CatalogFile &f) const;
    // 1.1: why the model cannot be downloaded here (too large for the computer, no room on the disk), or empty
    QString downloadProblem(const QString &id) const;

signals:
    void progress(const QString &id, qint64 done, qint64 total);
    void finished(const QString &id, bool ok, const QString &message);
    void changed();   // a model was installed or removed

private:
    void loadCatalog();
    void nextFile();
    void finish(bool ok, const QString &message);
    bool checkFile(const QString &path, const CatalogFile &f, QString *error) const;
    // the pieces of a download: every file, or every part of a file in parts
    struct Piece {
        CatalogFile file;          // what is downloaded (a part has its own name, asset, size and sum)
        QString joinInto;          // a part: the file it belongs to
        bool lastPart = false;
        qint64 before = 0;         // a part: the size of the parts before it
        CatalogFile whole;         // a part: the whole file (its size and sum are checked after the last part)
    };
    QList<Piece> pieces(const CatalogModel &m) const;
    bool placePiece(const Piece &piece, const QString &part, QString *error);
    QList<Piece> m_pieces;
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
