#include "ModelStore.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkProxyFactory>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QUrl>

namespace tp {

namespace {
const char *kRepo = "https://github.com/Shelin-boop/Quillbinder_Translation_Addon/releases/download/";
}

ModelStore::ModelStore(QObject *parent)
    : QObject(parent)
{
    loadCatalog();
}

void ModelStore::loadCatalog()
{
    QFile f(QStringLiteral(":/translation-proofreading/catalog.json"));
    if (!f.open(QIODevice::ReadOnly))
        return;
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    m_release = root.value(QStringLiteral("release")).toString();
    for (const QJsonValue &v : root.value(QStringLiteral("models")).toArray()) {
        const QJsonObject o = v.toObject();
        CatalogModel m;
        m.id = o.value(QStringLiteral("id")).toString();
        m.kind = o.value(QStringLiteral("kind")).toString();
        m.title = o.value(QStringLiteral("title")).toString();
        m.summary = o.value(QStringLiteral("summary")).toString();
        m.license = o.value(QStringLiteral("license")).toString();
        m.licenseUrl = o.value(QStringLiteral("licenseUrl")).toString();
        m.credit = o.value(QStringLiteral("credit")).toString();
        m.source = o.value(QStringLiteral("source")).toString();
        m.nonCommercial = o.value(QStringLiteral("nonCommercial")).toBool();
        m.memoryMB = o.value(QStringLiteral("memoryMB")).toInt();
        m.sizeMB = o.value(QStringLiteral("sizeMB")).toInt();
        const QJsonObject t = o.value(QStringLiteral("tokenizer")).toObject();
        m.tokenizer = t.value(QStringLiteral("type")).toString();
        m.spm = t.value(QStringLiteral("file")).toString();
        m.spmSource = t.value(QStringLiteral("source")).toString();
        m.spmTarget = t.value(QStringLiteral("target")).toString();
        for (const QJsonValue &fv : o.value(QStringLiteral("files")).toArray()) {
            const QJsonObject fo = fv.toObject();
            m.files << CatalogFile{fo.value(QStringLiteral("name")).toString(), fo.value(QStringLiteral("asset")).toString(),
                                   qint64(fo.value(QStringLiteral("size")).toDouble()),
                                   fo.value(QStringLiteral("sha256")).toString()};
        }
        m_models << m;
    }
}

QString ModelStore::modelsDir() const
{
    return m_dataDir + QStringLiteral("/models");
}

QList<CatalogModel> ModelStore::models(const QString &kind) const
{
    QList<CatalogModel> out;
    for (const CatalogModel &m : m_models)
        if (kind.isEmpty() || m.kind == kind)
            out << m;
    return out;
}

const CatalogModel *ModelStore::model(const QString &id) const
{
    for (const CatalogModel &m : m_models)
        if (m.id == id)
            return &m;
    return nullptr;
}

QString ModelStore::markerFile(const QString &id) const
{
    return modelsDir() + QLatin1Char('/') + id + QStringLiteral("/checked.txt");
}

bool ModelStore::isInstalled(const QString &id) const
{
    const CatalogModel *m = model(id);
    if (!m || !QFileInfo::exists(markerFile(id)))
        return false;
    // the sums were checked when the files came; here only that they are all still there, at their size
    for (const CatalogFile &f : m->files)
        if (QFileInfo(modelsDir() + QLatin1Char('/') + id + QLatin1Char('/') + f.name).size() != f.size)
            return false;
    return true;
}

ModelFiles ModelStore::files(const QString &id) const
{
    ModelFiles out;
    const CatalogModel *m = model(id);
    if (!m)
        return out;
    out.id = id;
    out.dir = modelsDir() + QLatin1Char('/') + id;
    out.tokenizer = m->tokenizer;
    out.spm = m->spm;
    out.spmSource = m->spmSource;
    out.spmTarget = m->spmTarget;
    return out;
}

bool ModelStore::remove(const QString &id, QString *error)
{
    if (m_job == id) {
        if (error)
            *error = tr("The model is being downloaded.");
        return false;
    }
    QDir d(modelsDir() + QLatin1Char('/') + id);
    if (d.exists() && !d.removeRecursively()) {
        if (error)
            *error = tr("The folder %1 could not be removed.").arg(d.path());
        return false;
    }
    emit changed();
    return true;
}

QString ModelStore::fileUrl(const CatalogFile &f) const
{
    const QString source = qEnvironmentVariable("QB_TP_MODEL_SOURCE");
    if (!source.isEmpty()) {
        if (source.contains(QStringLiteral("://")))
            return source + f.asset;
        return QUrl::fromLocalFile(source + QLatin1Char('/') + f.asset).toString();
    }
    return QLatin1String(kRepo) + m_release + QLatin1Char('/') + f.asset;
}

bool ModelStore::checkFile(const QString &path, const CatalogFile &f, QString *error) const
{
    QFile file(path);
    if (file.size() != f.size) {
        if (error)
            *error = tr("%1 has the wrong size (%2 instead of %3 bytes).").arg(f.name).arg(file.size()).arg(f.size);
        return false;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = tr("%1 cannot be read.").arg(f.name);
        return false;
    }
    QCryptographicHash h(QCryptographicHash::Sha256);
    if (!h.addData(&file) || QString::fromLatin1(h.result().toHex()) != f.sha256) {
        if (error)
            *error = tr("%1 is not the expected file (its checksum differs).").arg(f.name);
        return false;
    }
    return true;
}

void ModelStore::download(const QString &id)
{
    const CatalogModel *m = model(id);
    if (!m || busy())
        return;
    static bool tls = false;
    if (!tls) {
        tls = true;
        // Qt's TLS plugin comes with the add-on (the program has none); proxies as the system says
        if (!m_pluginDir.isEmpty() && QFileInfo::exists(m_pluginDir))
            QCoreApplication::addLibraryPath(m_pluginDir);
        QNetworkProxyFactory::setUseSystemConfiguration(true);
    }
    if (!m_net)
        m_net = new QNetworkAccessManager(this);
    QDir().mkpath(modelsDir() + QLatin1Char('/') + id);
    QFile::remove(markerFile(id));
    m_job = id;
    m_fileIndex = 0;
    m_doneBefore = 0;
    m_total = 0;
    for (const CatalogFile &f : m->files)
        m_total += f.size;
    m_cancelled = false;
    nextFile();
}

void ModelStore::nextFile()
{
    const CatalogModel *m = model(m_job);
    if (!m) {
        finish(false, tr("Unknown model."));
        return;
    }
    const QString dir = modelsDir() + QLatin1Char('/') + m_job;
    // the files already there and right are kept
    while (m_fileIndex < m->files.size()) {
        const CatalogFile &f = m->files.at(m_fileIndex);
        if (QFileInfo(dir + QLatin1Char('/') + f.name).size() == f.size
            && checkFile(dir + QLatin1Char('/') + f.name, f, nullptr)) {
            m_doneBefore += f.size;
            ++m_fileIndex;
            continue;
        }
        break;
    }
    if (m_fileIndex >= m->files.size()) {
        QSaveFile marker(markerFile(m_job));
        if (marker.open(QIODevice::WriteOnly)) {
            marker.write(QStringLiteral("%1 from %2: every file checked (size and SHA-256).\n").arg(m_job, m_release).toUtf8());
            marker.commit();
        }
        finish(true, tr("%1 is ready.").arg(m->title));
        return;
    }
    const CatalogFile f = m->files.at(m_fileIndex);
    const QString part = dir + QLatin1Char('/') + f.name + QStringLiteral(".part");
    const qint64 have = QFileInfo(part).size();
    if (have >= f.size) {
        QString error;
        QFile::remove(dir + QLatin1Char('/') + f.name);
        if (have == f.size && checkFile(part, f, &error) && QFile::rename(part, dir + QLatin1Char('/') + f.name)) {
            m_doneBefore += f.size;
            ++m_fileIndex;
            nextFile();
            return;
        }
        QFile::remove(part);   // too long or wrong: from the start
    }
    QNetworkRequest req{QUrl(fileUrl(f))};
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Quillbinder-Translation-Addon"));
    const qint64 resumeFrom = QFileInfo(part).size();
    if (resumeFrom > 0)
        req.setRawHeader("Range", "bytes=" + QByteArray::number(resumeFrom) + '-');
    m_file = new QFile(part, this);
    if (!m_file->open(QIODevice::ReadWrite)) {
        delete m_file;
        m_file = nullptr;
        finish(false, tr("The model cannot be saved in %1.").arg(dir));
        return;
    }
    m_file->seek(m_file->size());
    QNetworkReply *reply = m_net->get(req);
    m_reply = reply;
    auto first = std::make_shared<bool>(true);
    QFile *file = m_file;
    connect(reply, &QNetworkReply::readyRead, this, [reply, file, first, resumeFrom] {
        if (*first) {
            *first = false;
            // the server sent the whole file instead of the rest
            if (resumeFrom > 0 && reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 200) {
                file->resize(0);
                file->seek(0);
            }
        }
        file->write(reply->readAll());
    });
    connect(reply, &QNetworkReply::downloadProgress, this, [this, file](qint64, qint64) {
        emit progress(m_job, m_doneBefore + file->size(), m_total);
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, file, f, part, dir] {
        if (reply->isOpen())
            file->write(reply->readAll());
        file->close();
        file->deleteLater();
        if (m_file == file)
            m_file = nullptr;
        const QNetworkReply::NetworkError err = reply->error();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QString errText = reply->errorString();
        reply->deleteLater();
        if (m_cancelled) {
            finish(false, tr("Download stopped. Downloading again goes on where it stopped."));
            return;
        }
        if (err != QNetworkReply::NoError && status != 416) {
            finish(false, tr("The download failed: %1. Try again: it goes on where it stopped.").arg(errText));
            return;
        }
        QString error;
        QFile::remove(dir + QLatin1Char('/') + f.name);
        if (!checkFile(part, f, &error)) {
            QFile::remove(part);
            finish(false, tr("The downloaded file was not right and was deleted: %1").arg(error));
            return;
        }
        QFile::rename(part, dir + QLatin1Char('/') + f.name);
        m_doneBefore += f.size;
        ++m_fileIndex;
        nextFile();
    });
}

void ModelStore::cancel()
{
    if (busy() && m_reply) {
        m_cancelled = true;
        m_reply->abort();
    }
}

void ModelStore::finish(bool ok, const QString &message)
{
    const QString id = m_job;
    m_job.clear();
    m_reply = nullptr;
    emit finished(id, ok, message);
    if (ok)
        emit changed();
}

bool ModelStore::importFolder(const QString &id, const QString &folder, QString *error)
{
    const CatalogModel *m = model(id);
    if (!m || busy()) {
        if (error)
            *error = tr("Not possible now.");
        return false;
    }
    const QString dir = modelsDir() + QLatin1Char('/') + id;
    QDir().mkpath(dir);
    for (const CatalogFile &f : m->files) {
        QString src = folder + QLatin1Char('/') + f.asset;
        if (!QFileInfo::exists(src))
            src = folder + QLatin1Char('/') + f.name;
        if (!QFileInfo::exists(src)) {
            if (error)
                *error = tr("%1 is missing in %2.").arg(f.name, folder);
            return false;
        }
        if (!checkFile(src, f, error))
            return false;
        const QString dst = dir + QLatin1Char('/') + f.name;
        if (QFileInfo(src).canonicalFilePath() != QFileInfo(dst).canonicalFilePath()) {
            QFile::remove(dst);
            if (!QFile::copy(src, dst)) {
                if (error)
                    *error = tr("%1 could not be copied to %2.").arg(f.name, dir);
                return false;
            }
        }
    }
    QSaveFile marker(markerFile(id));
    if (marker.open(QIODevice::WriteOnly)) {
        marker.write(QStringLiteral("%1 from the folder %2: every file checked (size and SHA-256).\n").arg(id, folder).toUtf8());
        marker.commit();
    }
    emit changed();
    return true;
}

} // namespace tp
