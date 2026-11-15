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
#include <QStorageInfo>
#include <QUrl>

namespace tp {

namespace {
const char *kRepo = "https://github.com/Shelin-boop/Quillbinder_Translation_Addon/releases/download/";

CatalogFile readFile(const QJsonObject &fo)
{
    CatalogFile f{fo.value(QStringLiteral("name")).toString(), fo.value(QStringLiteral("asset")).toString(),
                  qint64(fo.value(QStringLiteral("size")).toDouble()), fo.value(QStringLiteral("sha256")).toString(), {}};
    for (const QJsonValue &pv : fo.value(QStringLiteral("parts")).toArray()) {
        const QJsonObject po = pv.toObject();
        f.parts << CatalogFile{QString(), po.value(QStringLiteral("asset")).toString(),
                               qint64(po.value(QStringLiteral("size")).toDouble()),
                               po.value(QStringLiteral("sha256")).toString(), {}};
    }
    return f;
}
} // namespace

// ---------------------------------------------------------------- the computer's memory (1.1)

Machine Machine::now()
{
    Machine m;
#ifdef TP_TEST_SWITCHES
    const QString fake = qEnvironmentVariable("TP_TEST_MEMINFO_MB");
    if (!fake.isEmpty()) {
        m.totalMB = fake.section(QLatin1Char(','), 0, 0).toLongLong();
        m.availableMB = fake.section(QLatin1Char(','), 1, 1).toLongLong();
        return m;
    }
#endif
    QFile f(QStringLiteral("/proc/meminfo"));
    if (f.open(QIODevice::ReadOnly)) {
        for (const QByteArray &line : f.readAll().split('\n')) {
            const QList<QByteArray> p = line.simplified().split(' ');
            if (p.size() < 2)
                continue;
            if (p.at(0) == "MemTotal:")
                m.totalMB = p.at(1).toLongLong() / 1024;
            else if (p.at(0) == "MemAvailable:")
                m.availableMB = p.at(1).toLongLong() / 1024;
        }
    }
    return m;
}

QString memoryProblem(const CatalogModel &m, const Machine &machine, bool loadingNow, qint64 loadedMB)
{
    if (machine.totalMB <= 0 || m.memoryMB <= 0)
        return QString();   // nothing known: no guard (not Linux?)
    const auto gb = [](qint64 mb) { return QString::number(mb / 1024.0, 'f', 1); };
    if (m.memoryMB > machine.totalMB * 3 / 4)
        return QCoreApplication::translate("tp::ModelStore",
                                           "%1 needs about %2 GB of memory; this computer has %3 GB, too little for it.")
            .arg(m.title, gb(m.memoryMB), gb(machine.totalMB));
    const qint64 margin = 1024;   // for the system and the program
    if (loadingNow && m.memoryMB + margin > machine.availableMB + loadedMB)
        return QCoreApplication::translate("tp::ModelStore",
                                           "%1 needs about %2 GB of free memory, and %3 GB are free now. Close other "
                                           "programs or choose a smaller model.")
            .arg(m.title, gb(m.memoryMB), gb(machine.availableMB + loadedMB));
    return QString();
}

// ---------------------------------------------------------------- the catalog

ModelStore::ModelStore(QObject *parent)
    : QObject(parent)
{
    loadCatalog();
}

void ModelStore::loadCatalog()
{
    QFile f(QStringLiteral(":/translation-proofreading/catalog.json"));
#ifdef TP_TEST_SWITCHES
    if (qEnvironmentVariableIsSet("TP_TEST_CATALOG"))   // a catalog of the checks (a file in parts)
        f.setFileName(qEnvironmentVariable("TP_TEST_CATALOG"));
#endif
    if (!f.open(QIODevice::ReadOnly))
        return;
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    m_release = root.value(QStringLiteral("release")).toString();
    for (const QJsonValue &v : root.value(QStringLiteral("models")).toArray()) {
        const QJsonObject o = v.toObject();
        CatalogModel m;
        m.id = o.value(QStringLiteral("id")).toString();
        for (const QJsonValue &k : o.value(QStringLiteral("kinds")).toArray())
            m.kinds << k.toString();
        if (m.kinds.isEmpty())
            m.kinds << o.value(QStringLiteral("kind")).toString();   // the catalog of 1.0
        m.type = o.value(QStringLiteral("type")).toString(QStringLiteral("seq2seq"));
        m.release = o.value(QStringLiteral("release")).toString(m_release);
        m.title = o.value(QStringLiteral("title")).toString();
        m.summary = o.value(QStringLiteral("summary")).toString();
        m.license = o.value(QStringLiteral("license")).toString();
        m.licenseUrl = o.value(QStringLiteral("licenseUrl")).toString();
        m.credit = o.value(QStringLiteral("credit")).toString();
        m.source = o.value(QStringLiteral("source")).toString();
        m.nonCommercial = o.value(QStringLiteral("nonCommercial")).toBool();
        m.memoryMB = o.value(QStringLiteral("memoryMB")).toInt();
        m.sizeMB = o.value(QStringLiteral("sizeMB")).toInt();
        m.sourceToken = o.value(QStringLiteral("sourceToken")).toString();
        m.thinkTags = o.value(QStringLiteral("chat")).toObject().value(QStringLiteral("thinkTags")).toBool();
        const QJsonObject t = o.value(QStringLiteral("tokenizer")).toObject();
        m.tokenizer = t.value(QStringLiteral("type")).toString();
        m.spm = t.value(QStringLiteral("file")).toString();
        m.spmSource = t.value(QStringLiteral("source")).toString();
        m.spmTarget = t.value(QStringLiteral("target")).toString();
        for (const QJsonValue &fv : o.value(QStringLiteral("files")).toArray())
            m.files << readFile(fv.toObject());
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
        if (kind.isEmpty() || m.is(kind))
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
    out.type = m->type;
    out.tokenizer = m->tokenizer;
    out.spm = m->spm;
    out.spmSource = m->spmSource;
    out.spmTarget = m->spmTarget;
    out.sourceToken = m->sourceToken;
    out.thinkTags = m->thinkTags;
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

QString ModelStore::fileUrl(const CatalogModel &m, const CatalogFile &f) const
{
    const QString source = qEnvironmentVariable("QB_TP_MODEL_SOURCE");
    if (!source.isEmpty()) {
        if (source.contains(QStringLiteral("://")))
            return source + f.asset;
        return QUrl::fromLocalFile(source + QLatin1Char('/') + f.asset).toString();
    }
    return QLatin1String(kRepo) + m.release + QLatin1Char('/') + f.asset;
}

QString ModelStore::downloadProblem(const QString &id) const
{
    const CatalogModel *m = model(id);
    if (!m)
        return tr("Unknown model.");
    const QString memory = memoryProblem(*m, Machine::now(), false);
    if (!memory.isEmpty())
        return memory;
    // room for the files, and for one part more while a file in parts is joined
    qint64 need = 0, biggestPart = 0;
    for (const CatalogFile &f : m->files) {
        need += f.size;
        for (const CatalogFile &p : f.parts)
            biggestPart = qMax(biggestPart, p.size);
    }
    QDir().mkpath(modelsDir());
    const QStorageInfo disk(modelsDir());
    const qint64 free = disk.isValid() ? disk.bytesAvailable() : -1;
    const qint64 margin = 512LL * 1024 * 1024;
    if (free >= 0 && free < need + biggestPart + margin)
        return tr("%1 needs %2 GB on the disk, and %3 GB are free in %4.")
            .arg(m->title, QString::number((need + biggestPart + margin) / 1e9, 'f', 1),
                 QString::number(free / 1e9, 'f', 1), modelsDir());
    return QString();
}

bool ModelStore::checkFile(const QString &path, const CatalogFile &f, QString *error) const
{
    QFile file(path);
    const QString name = f.name.isEmpty() ? f.asset : f.name;
    if (file.size() != f.size) {
        if (error)
            *error = tr("%1 has the wrong size (%2 instead of %3 bytes).").arg(name).arg(file.size()).arg(f.size);
        return false;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = tr("%1 cannot be read.").arg(name);
        return false;
    }
    QCryptographicHash h(QCryptographicHash::Sha256);
    if (!h.addData(&file) || QString::fromLatin1(h.result().toHex()) != f.sha256) {
        if (error)
            *error = tr("%1 is not the expected file (its checksum differs).").arg(name);
        return false;
    }
    return true;
}

// ---------------------------------------------------------------- download

QList<ModelStore::Piece> ModelStore::pieces(const CatalogModel &m) const
{
    QList<Piece> out;
    for (const CatalogFile &f : m.files) {
        if (f.parts.isEmpty()) {
            out << Piece{f, QString(), false, 0, CatalogFile()};
            continue;
        }
        qint64 before = 0;
        for (int i = 0; i < f.parts.size(); ++i) {
            CatalogFile p = f.parts.at(i);
            p.name = QStringLiteral("%1.part%2").arg(f.name).arg(i + 1);
            out << Piece{p, f.name, i == f.parts.size() - 1, before, f};
            before += p.size;
        }
    }
    return out;
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
    const QString problem = downloadProblem(id);
    if (!problem.isEmpty()) {
        m_job = id;
        finish(false, problem);
        return;
    }
    QDir().mkpath(modelsDir() + QLatin1Char('/') + id);
    QFile::remove(markerFile(id));
    m_job = id;
    m_pieces = pieces(*m);
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
    // the files already there and right are kept; a file in parts: the parts already joined
    while (m_fileIndex < m_pieces.size()) {
        const Piece &p = m_pieces.at(m_fileIndex);
        if (p.joinInto.isEmpty()) {
            if (QFileInfo(dir + QLatin1Char('/') + p.file.name).size() == p.file.size
                && checkFile(dir + QLatin1Char('/') + p.file.name, p.file, nullptr)) {
                m_doneBefore += p.file.size;
                ++m_fileIndex;
                continue;
            }
        } else if (QFileInfo(dir + QLatin1Char('/') + p.joinInto).size() == p.whole.size
                   || QFileInfo(dir + QLatin1Char('/') + p.joinInto + QStringLiteral(".joining")).size()
                          >= p.before + p.file.size) {
            m_doneBefore += p.file.size;
            ++m_fileIndex;
            continue;
        }
        break;
    }
    if (m_fileIndex >= m_pieces.size()) {
        // a whole file still waiting after its last part (an interrupted join): checked now
        for (const CatalogFile &f : m->files) {
            if (f.parts.isEmpty() || QFileInfo(dir + QLatin1Char('/') + f.name).size() == f.size)
                continue;
            QString error;
            const QString joining = dir + QLatin1Char('/') + f.name + QStringLiteral(".joining");
            if (!checkFile(joining, f, &error)) {
                QFile::remove(joining);
                finish(false, tr("The downloaded file was not right and was deleted: %1").arg(error));
                return;
            }
            QFile::rename(joining, dir + QLatin1Char('/') + f.name);
        }
        QSaveFile marker(markerFile(m_job));
        if (marker.open(QIODevice::WriteOnly)) {
            marker.write(QStringLiteral("%1 from %2: every file checked (size and SHA-256).\n").arg(m_job, m->release).toUtf8());
            marker.commit();
        }
        finish(true, tr("%1 is ready.").arg(m->title));
        return;
    }
    const Piece piece = m_pieces.at(m_fileIndex);
    const CatalogFile f = piece.file;
    const QString part = dir + QLatin1Char('/') + f.name + QStringLiteral(".part");
    const qint64 have = QFileInfo(part).size();
    if (have >= f.size) {
        QString error;
        if (have == f.size && checkFile(part, f, &error) && placePiece(piece, part, &error)) {
            m_doneBefore += f.size;
            ++m_fileIndex;
            nextFile();
            return;
        }
        QFile::remove(part);   // too long or wrong: from the start
    }
    QNetworkRequest req{QUrl(fileUrl(*m, f))};
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
    connect(reply, &QNetworkReply::finished, this, [this, reply, file, f, part, piece] {
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
        if (!checkFile(part, f, &error)) {
            QFile::remove(part);
            finish(false, tr("The downloaded file was not right and was deleted: %1").arg(error));
            return;
        }
        if (!placePiece(piece, part, &error)) {
            finish(false, error);
            return;
        }
        m_doneBefore += f.size;
        ++m_fileIndex;
        nextFile();
    });
}

bool ModelStore::placePiece(const Piece &piece, const QString &part, QString *error)
{
    const QString dir = QFileInfo(part).absolutePath();
    if (piece.joinInto.isEmpty()) {
        QFile::remove(dir + QLatin1Char('/') + piece.file.name);
        return QFile::rename(part, dir + QLatin1Char('/') + piece.file.name);
    }
    // a part: added to the file being joined, then deleted (so the disk holds one part more at most)
    const QString joining = dir + QLatin1Char('/') + piece.joinInto + QStringLiteral(".joining");
    QFile out(joining);
    if (!out.open(QIODevice::ReadWrite)) {
        if (error)
            *error = tr("%1 cannot be written.").arg(joining);
        return false;
    }
    out.resize(piece.before);   // a part added before an interruption is written again
    out.seek(piece.before);
    QFile in(part);
    if (!in.open(QIODevice::ReadOnly)) {
        if (error)
            *error = tr("%1 cannot be read.").arg(part);
        return false;
    }
    while (!in.atEnd()) {
        const QByteArray block = in.read(8 * 1024 * 1024);
        if (out.write(block) != block.size()) {
            if (error)
                *error = tr("%1 cannot be written (is the disk full?).").arg(joining);
            return false;
        }
    }
    in.close();
    out.close();
    QFile::remove(part);
    if (piece.lastPart) {
        QString e;
        if (!checkFile(joining, piece.whole, &e)) {
            QFile::remove(joining);
            if (error)
                *error = tr("The downloaded file was not right and was deleted: %1").arg(e);
            return false;
        }
        QFile::remove(dir + QLatin1Char('/') + piece.joinInto);
        return QFile::rename(joining, dir + QLatin1Char('/') + piece.joinInto);
    }
    return true;
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
        const QString dst = dir + QLatin1Char('/') + f.name;
        QString src = folder + QLatin1Char('/') + f.asset;
        if (f.asset.isEmpty() || !QFileInfo::exists(src))
            src = folder + QLatin1Char('/') + f.name;
        if (!QFileInfo::exists(src) && !f.parts.isEmpty()) {
            // the parts as on the release: joined here
            const QString joining = dst + QStringLiteral(".joining");
            QFile::remove(joining);
            QFile out(joining);
            if (!out.open(QIODevice::WriteOnly)) {
                if (error)
                    *error = tr("%1 cannot be written.").arg(joining);
                return false;
            }
            for (const CatalogFile &p : f.parts) {
                const QString ps = folder + QLatin1Char('/') + p.asset;
                if (!QFileInfo::exists(ps)) {
                    if (error)
                        *error = tr("%1 is missing in %2.").arg(p.asset, folder);
                    return false;
                }
                if (!checkFile(ps, p, error))
                    return false;
                QFile in(ps);
                if (!in.open(QIODevice::ReadOnly)) {
                    if (error)
                        *error = tr("%1 cannot be read.").arg(ps);
                    out.close();
                    QFile::remove(joining);
                    return false;
                }
                while (!in.atEnd())
                    out.write(in.read(8 * 1024 * 1024));
            }
            out.close();
            if (!checkFile(joining, f, error)) {
                QFile::remove(joining);
                return false;
            }
            QFile::remove(dst);
            QFile::rename(joining, dst);
            continue;
        }
        if (!QFileInfo::exists(src)) {
            if (error)
                *error = tr("%1 is missing in %2.").arg(f.name, folder);
            return false;
        }
        if (!checkFile(src, f, error))
            return false;
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
