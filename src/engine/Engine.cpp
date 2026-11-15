// The engine of the add-on "Translation and Proofreading": CTranslate2 (with oneDNN) runs the models,
// SentencePiece cuts the text into the models' pieces. Built into libqb-tp-engine.so; see EngineApi.h.
#include "EngineApi.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMutex>
#include <QMutexLocker>
#include <QSet>
#include <QThread>

#include <ctranslate2/translator.h>
#include <sentencepiece_processor.h>

#include <memory>
#include <stdexcept>

namespace {

using Tokens = std::vector<std::string>;

struct Slot {
    tp::ModelFiles files;
    std::unique_ptr<ctranslate2::Translator> translator;
    sentencepiece::SentencePieceProcessor source;   // t5: both sides
    sentencepiece::SentencePieceProcessor target;   // marian only
    std::vector<std::vector<std::string>> bans;      // the dash sequences this vocabulary has
};

QString qs(const std::string &s) { return QString::fromStdString(s); }
std::string ss(const QString &s) { return s.toStdString(); }

class Engine : public tp::EngineInterface
{
public:
    bool load(const QString &slot, const tp::ModelFiles &model, int threads, QString *error) override
    {
        QMutexLocker lock(&m_mutex);
        if (const auto it = m_slots.find(slot);
            it != m_slots.end() && it->second->files.id == model.id && it->second->files.dir == model.dir)
            return true;
        m_slots.erase(slot);
        auto s = std::make_unique<Slot>();
        s->files = model;
        try {
            const QString src = model.tokenizer == QLatin1String("marian") ? model.spmSource : model.spm;
            if (!s->source.Load(ss(model.dir + QLatin1Char('/') + src)).ok())
                return fail(error, QStringLiteral("cannot read %1").arg(src));
            if (model.tokenizer == QLatin1String("marian")
                && !s->target.Load(ss(model.dir + QLatin1Char('/') + model.spmTarget)).ok())
                return fail(error, QStringLiteral("cannot read %1").arg(model.spmTarget));
            ctranslate2::models::ModelLoader loader(ss(model.dir));
            loader.device = ctranslate2::Device::CPU;
            loader.compute_type = ctranslate2::ComputeType::INT8;
            ctranslate2::ReplicaPoolConfig config;
            config.num_threads_per_replica = threads > 0 ? size_t(threads) : size_t(QThread::idealThreadCount());
            s->translator = std::make_unique<ctranslate2::Translator>(loader, config);
            s->bans = dashBans(model.dir);
        } catch (const std::exception &e) {
            return fail(error, QString::fromLocal8Bit(e.what()));
        }
        m_slots[slot] = std::move(s);
        return true;
    }

    void unload(const QString &slot) override
    {
        QMutexLocker lock(&m_mutex);
        m_slots.erase(slot);
    }

    QString loaded(const QString &slot) const override
    {
        QMutexLocker lock(&m_mutex);
        const auto it = m_slots.find(slot);
        return it == m_slots.end() ? QString() : it->second->files.id;
    }

    QStringList run(const QString &slot, const QStringList &texts, const tp::RunOptions &o,
                    const std::function<bool(int, int)> &progress, QString *error) override
    {
        QMutexLocker lock(&m_mutex);
        Slot *s = slotFor(slot, error);
        QStringList out;
        if (!s)
            return out;
        const int batch = qMax(1, o.batch);
        try {
            for (int start = 0; start < texts.size(); start += batch) {
                std::vector<Tokens> source;
                for (int i = start; i < qMin(int(texts.size()), start + batch); ++i)
                    source.push_back(encodeSource(*s, o.prefix + texts.at(i)));
                const auto results = s->translator->translate_batch(source, options(*s, o), size_t(batch));
                for (const auto &r : results)
                    out << decodeTarget(*s, r.hypotheses.empty() ? Tokens() : r.hypotheses.front());
                if (progress && !progress(int(out.size()), int(texts.size()))) {
                    while (out.size() < texts.size())
                        out << QString();
                    break;
                }
            }
        } catch (const std::exception &e) {
            fail(error, QString::fromLocal8Bit(e.what()));
            return {};
        }
        return out;
    }

    QList<tp::Alternative> alternatives(const QString &slot, const QString &sourceText, const QString &current,
                                        int wordIndex, const tp::RunOptions &o, int count, QString *error) override
    {
        QMutexLocker lock(&m_mutex);
        QList<tp::Alternative> out;
        Slot *s = slotFor(slot, error);
        if (!s || wordIndex < 0)
            return out;
        try {
            // the pieces of the current text, grouped into words at the pieces that start one ("▁")
            const Tokens pieces = encodeTarget(*s, current);
            std::vector<Tokens> words;
            for (const std::string &p : pieces) {
                if (words.empty() || p.rfind("\xe2\x96\x81", 0) == 0)
                    words.emplace_back();
                words.back().push_back(p);
            }
            Tokens prefix;
            for (int i = 0; i < wordIndex && i < int(words.size()); ++i)
                prefix.insert(prefix.end(), words[size_t(i)].begin(), words[size_t(i)].end());
            const Tokens source = encodeSource(*s, o.prefix + sourceText);
            // step 1: which pieces could stand at this place (one step of the model)
            ctranslate2::TranslationOptions first = options(*s, o);
            first.return_alternatives = true;
            first.num_hypotheses = size_t(2 * count);
            first.beam_size = size_t(2 * count);
            first.max_decoding_length = prefix.size() + 1;
            const auto r1 = s->translator->translate_batch({source}, {prefix}, first);
            std::vector<std::string> starts;
            for (const Tokens &h : r1.front().hypotheses) {
                if (h.size() > prefix.size()) {
                    const std::string &t = h[prefix.size()];
                    if (t != "</s>" && std::find(starts.begin(), starts.end(), t) == starts.end())
                        starts.push_back(t);
                }
            }
            if (starts.empty())
                return out;
            if (starts.size() > size_t(count) + 2)
                starts.resize(size_t(count) + 2);
            // step 2: the sentence after each of them, all in one batch (the model loses its way when it goes
            // on straight from an alternative, so each one is finished on its own)
            std::vector<Tokens> sources(starts.size(), source), prefixes;
            for (const std::string &t : starts) {
                Tokens p = prefix;
                p.push_back(t);
                prefixes.push_back(p);
            }
            ctranslate2::TranslationOptions rest = options(*s, o);
            rest.beam_size = 2;
            const auto r2 = s->translator->translate_batch(sources, prefixes, rest);
            QSet<QString> seen;
            for (const auto &r : r2) {
                if (r.hypotheses.empty())
                    continue;
                const Tokens &h = r.hypotheses.front();
                Tokens word;
                for (size_t i = prefix.size(); i < h.size(); ++i) {
                    if (i > prefix.size() && h[i].rfind("\xe2\x96\x81", 0) == 0)
                        break;
                    word.push_back(h[i]);
                }
                const QString w = decodeTarget(*s, word);
                if (w.isEmpty() || seen.contains(w))
                    continue;
                seen.insert(w);
                out << tp::Alternative{w, decodeTarget(*s, h)};
            }
        } catch (const std::exception &e) {
            fail(error, QString::fromLocal8Bit(e.what()));
            return {};
        }
        return out;
    }

    QString version() const override { return QStringLiteral("CTranslate2 4.9.0, oneDNN 3.9.1, SentencePiece 0.2.1"); }

private:
    static bool fail(QString *error, const QString &e)
    {
        if (error)
            *error = e;
        return false;
    }

    Slot *slotFor(const QString &slot, QString *error)
    {
        const auto it = m_slots.find(slot);
        if (it == m_slots.end()) {
            fail(error, QStringLiteral("no model loaded"));
            return nullptr;
        }
        return it->second.get();
    }

    static std::vector<std::vector<std::string>> dashBans(const QString &dir)
    {
        // a dash between words in every form this vocabulary knows; a hyphen inside a word ("well-known") has no
        // space before it and stays allowed
        QSet<QString> vocab;
        QFile f(dir + QStringLiteral("/shared_vocabulary.json"));
        if (f.open(QIODevice::ReadOnly))
            for (const QJsonValue &v : QJsonDocument::fromJson(f.readAll()).array())
                vocab.insert(v.toString());
        const QString sp = QStringLiteral("▁");
        const QList<QStringList> wanted = {{QStringLiteral("—")}, {sp + QStringLiteral("—")},
                                           {QStringLiteral("–")}, {sp + QStringLiteral("–")},
                                           {sp + QStringLiteral("-")}, {sp, QStringLiteral("-")},
                                           {sp, QStringLiteral("—")}, {sp, QStringLiteral("–")}};
        std::vector<std::vector<std::string>> out;
        for (const QStringList &seq : wanted) {
            bool all = true;
            for (const QString &t : seq)
                all = all && vocab.contains(t);
            if (!all)
                continue;
            std::vector<std::string> v;
            for (const QString &t : seq)
                v.push_back(ss(t));
            out.push_back(v);
        }
        return out;
    }

    static ctranslate2::TranslationOptions options(const Slot &s, const tp::RunOptions &o)
    {
        ctranslate2::TranslationOptions t;
        t.beam_size = size_t(qMax(1, o.beam));
        t.max_decoding_length = 256;
        if (o.banDashes)
            t.suppress_sequences = s.bans;
        return t;
    }

    static Tokens encodeSource(const Slot &s, const QString &text)
    {
        Tokens t;
        s.source.Encode(ss(text), &t);
        t.push_back("</s>");
        return t;
    }

    static Tokens encodeTarget(const Slot &s, const QString &text)
    {
        Tokens t;
        (s.files.tokenizer == QLatin1String("marian") ? s.target : s.source).Encode(ss(text), &t);
        return t;
    }

    static QString decodeTarget(const Slot &s, const Tokens &pieces)
    {
        Tokens clean;
        for (const std::string &p : pieces)
            if (p != "</s>" && p != "<pad>" && p != "<unk>")
                clean.push_back(p);
        std::string text;
        (s.files.tokenizer == QLatin1String("marian") ? s.target : s.source).Decode(clean, &text);
        return qs(text).trimmed();
    }

    mutable QMutex m_mutex;
    std::map<QString, std::unique_ptr<Slot>> m_slots;
};

} // namespace

extern "C" Q_DECL_EXPORT tp::EngineInterface *qb_tp_engine_create()
{
    return new Engine;
}
