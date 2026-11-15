// The engine of the add-on "Translation and Proofreading": CTranslate2 (with oneDNN) runs the models; SentencePiece
// cuts the text into the pieces of the T5 and Marian models, Bpe.cpp into those of the chat models (1.1). Built into
// libqb-tp-engine.so; see EngineApi.h.
#include "EngineApi.h"
#include "Bpe.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMutex>
#include <QMutexLocker>
#include <QRegularExpression>
#include <QSet>
#include <QThread>

#include <ctranslate2/generator.h>
#include <ctranslate2/translator.h>
#include <sentencepiece_processor.h>

#include <map>
#include <memory>
#include <stdexcept>

namespace {

using Tokens = std::vector<std::string>;

// One loaded model; both slots use the same one when the writer chose a chat model for both (1.1).
struct Model {
    tp::ModelFiles files;
    std::unique_ptr<ctranslate2::Translator> translator;   // seq2seq
    std::unique_ptr<ctranslate2::Generator> generator;     // chat
    sentencepiece::SentencePieceProcessor source;   // t5: both sides
    sentencepiece::SentencePieceProcessor target;   // marian only
    tp::BpeTokenizer bpe;                            // chat
    std::vector<std::vector<std::string>> bans;      // the dash sequences this vocabulary has
    bool chat() const { return generator != nullptr; }
};

QString qs(const std::string &s) { return QString::fromStdString(s); }
std::string ss(const QString &s) { return s.toStdString(); }

const std::string kImEnd = "<|im_end|>";
const std::string kEndOfText = "<|endoftext|>";

class Engine : public tp::EngineInterface
{
public:
    bool load(const QString &slot, const tp::ModelFiles &model, int threads, QString *error) override
    {
        QMutexLocker lock(&m_mutex);
        if (const auto it = m_slots.find(slot);
            it != m_slots.end() && it->second->files.id == model.id && it->second->files.dir == model.dir)
            return true;
        m_slots.erase(slot);   // the model loaded there before goes (unless the other slot keeps it)
        for (const auto &[name, other] : m_slots)
            if (other->files.id == model.id && other->files.dir == model.dir) {
                m_slots[slot] = other;
                return true;
            }
        auto m = std::make_shared<Model>();
        m->files = model;
        try {
            ctranslate2::models::ModelLoader loader(ss(model.dir));
            loader.device = ctranslate2::Device::CPU;
            loader.compute_type = ctranslate2::ComputeType::INT8;
            ctranslate2::ReplicaPoolConfig config;
            config.num_threads_per_replica = threads > 0 ? size_t(threads) : size_t(QThread::idealThreadCount());
            if (model.type == QLatin1String("chat")) {
                QString e;
                if (!m->bpe.load(model.dir + QLatin1Char('/') + model.spm, &e))
                    return fail(error, e);
                m->generator = std::make_unique<ctranslate2::Generator>(loader, config);
                m->bans = chatDashBans(m->bpe, model.dir);
            } else {
                const QString src = model.tokenizer == QLatin1String("marian") ? model.spmSource : model.spm;
                if (!m->source.Load(ss(model.dir + QLatin1Char('/') + src)).ok())
                    return fail(error, QStringLiteral("cannot read %1").arg(src));
                if (model.tokenizer == QLatin1String("marian")
                    && !m->target.Load(ss(model.dir + QLatin1Char('/') + model.spmTarget)).ok())
                    return fail(error, QStringLiteral("cannot read %1").arg(model.spmTarget));
                m->translator = std::make_unique<ctranslate2::Translator>(loader, config);
                m->bans = dashBans(model.dir);
            }
        } catch (const std::exception &e) {
            return fail(error, QString::fromLocal8Bit(e.what()));
        }
        m_slots[slot] = std::move(m);
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
        Model *s = slotFor(slot, error);
        QStringList out;
        if (!s)
            return out;
        const int batch = s->chat() ? qBound(1, o.batch, 4) : qMax(1, o.batch);
        try {
            for (int start = 0; start < texts.size(); start += batch) {
                const QStringList part = texts.mid(start, batch);
                out << (s->chat() ? chatBatch(*s, part, o) : seq2seqBatch(*s, part, o));
                const bool stop = (progress && !progress(int(out.size()), int(texts.size()))) || (o.cancel && o.cancel->load());
                if (stop) {
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
        Model *s = slotFor(slot, error);
        if (!s || wordIndex < 0)
            return {};
        try {
            return s->chat() ? chatAlternatives(*s, sourceText, current, wordIndex, o, count)
                             : seq2seqAlternatives(*s, sourceText, current, wordIndex, o, count);
        } catch (const std::exception &e) {
            fail(error, QString::fromLocal8Bit(e.what()));
            return {};
        }
    }

    QString version() const override
    {
        return QStringLiteral("CTranslate2 4.9.0, oneDNN 3.9.1, SentencePiece 0.2.1, BPE tokenizer for chat models");
    }

private:
    static bool fail(QString *error, const QString &e)
    {
        if (error)
            *error = e;
        return false;
    }

    Model *slotFor(const QString &slot, QString *error)
    {
        const auto it = m_slots.find(slot);
        if (it == m_slots.end()) {
            fail(error, QStringLiteral("no model loaded"));
            return nullptr;
        }
        return it->second.get();
    }

    // ---------------------------------------------------------------- seq2seq (CoEdIT, OPUS-MT)

    QStringList seq2seqBatch(const Model &s, const QStringList &texts, const tp::RunOptions &o)
    {
        std::vector<Tokens> source;
        for (const QString &t : texts)
            source.push_back(encodeSource(s, o.prefix + t));
        const auto results = s.translator->translate_batch(source, options(s, o), source.size());
        QStringList out;
        for (const auto &r : results)
            out << decodeTarget(s, r.hypotheses.empty() ? Tokens() : r.hypotheses.front());
        return out;
    }

    QList<tp::Alternative> seq2seqAlternatives(const Model &s, const QString &sourceText, const QString &current,
                                               int wordIndex, const tp::RunOptions &o, int count)
    {
        QList<tp::Alternative> out;
        // the pieces of the current text, grouped into words at the pieces that start one ("▁")
        const Tokens pieces = encodeTarget(s, current);
        std::vector<Tokens> words;
        for (const std::string &p : pieces) {
            if (words.empty() || p.rfind("\xe2\x96\x81", 0) == 0)
                words.emplace_back();
            words.back().push_back(p);
        }
        Tokens prefix;
        for (int i = 0; i < wordIndex && i < int(words.size()); ++i)
            prefix.insert(prefix.end(), words[size_t(i)].begin(), words[size_t(i)].end());
        const Tokens source = encodeSource(s, o.prefix + sourceText);
        // step 1: which pieces could stand at this place (one step of the model)
        ctranslate2::TranslationOptions first = options(s, o);
        first.return_alternatives = true;
        first.num_hypotheses = size_t(2 * count);
        first.beam_size = size_t(2 * count);
        first.max_decoding_length = prefix.size() + 1;
        const auto r1 = s.translator->translate_batch({source}, {prefix}, first);
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
        // step 2: the sentence after each of them, all in one batch (the model loses its way when it goes on
        // straight from an alternative, so each one is finished on its own)
        std::vector<Tokens> sources(starts.size(), source), prefixes;
        for (const std::string &t : starts) {
            Tokens p = prefix;
            p.push_back(t);
            prefixes.push_back(p);
        }
        ctranslate2::TranslationOptions rest = options(s, o);
        rest.beam_size = 2;
        const auto r2 = s.translator->translate_batch(sources, prefixes, rest);
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
            const QString w = decodeTarget(s, word);
            if (w.isEmpty() || seen.contains(w))
                continue;
            seen.insert(w);
            out << tp::Alternative{w, decodeTarget(s, h)};
        }
        return out;
    }

    static std::vector<std::vector<std::string>> dashBans(const QString &dir)
    {
        // a dash between words in every form this vocabulary knows; a hyphen inside a word ("well-known") has no
        // space before it and stays allowed
        QSet<QString> vocab;
        QFile f(dir + QStringLiteral("/shared_vocabulary.json"));
        if (!f.exists())
            f.setFileName(dir + QStringLiteral("/target_vocabulary.json"));
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

    static ctranslate2::TranslationOptions options(const Model &s, const tp::RunOptions &o)
    {
        ctranslate2::TranslationOptions t;
        t.beam_size = size_t(qMax(1, o.beam));
        t.max_decoding_length = 256;
        if (o.banDashes)
            t.suppress_sequences = s.bans;
        return t;
    }

    static Tokens encodeSource(const Model &s, const QString &text)
    {
        Tokens t;
        if (!s.files.sourceToken.isEmpty())
            t.push_back(ss(s.files.sourceToken));   // the target language of a model with several (">>eng<<")
        Tokens pieces;
        s.source.Encode(ss(text), &pieces);
        t.insert(t.end(), pieces.begin(), pieces.end());
        t.push_back("</s>");
        return t;
    }

    static Tokens encodeTarget(const Model &s, const QString &text)
    {
        Tokens t;
        (s.files.tokenizer == QLatin1String("marian") ? s.target : s.source).Encode(ss(text), &t);
        return t;
    }

    static QString decodeTarget(const Model &s, const Tokens &pieces)
    {
        Tokens clean;
        for (const std::string &p : pieces)
            if (p != "</s>" && p != "<pad>" && p != "<unk>")
                clean.push_back(p);
        std::string text;
        (s.files.tokenizer == QLatin1String("marian") ? s.target : s.source).Decode(clean, &text);
        return qs(text).trimmed();
    }

    // ---------------------------------------------------------------- chat models (1.1: Qwen)

    // The system message, the same for every text of a run: the model state after it is kept (static prompt).
    static QString systemPart(const tp::RunOptions &o)
    {
        return QStringLiteral("<|im_start|>system\n%1<|im_end|>\n").arg(o.system);
    }

    static QString userPart(const Model &s, const QString &text)
    {
        QString p = QStringLiteral("<|im_start|>user\n%1<|im_end|>\n<|im_start|>assistant\n").arg(text);
        if (s.files.thinkTags)
            p += QStringLiteral("<think>\n\n</think>\n\n");
        return p;
    }

    ctranslate2::GenerationOptions chatOptions(const Model &s, const tp::RunOptions &o, const Tokens &system) const
    {
        ctranslate2::GenerationOptions g;
        g.beam_size = 1;
        g.sampling_topk = 1;   // greedy: the same text gives the same answer
        g.include_prompt_in_result = false;
        g.end_token = std::vector<std::string>{kImEnd, kEndOfText};
        g.static_prompt = system;
        g.cache_static_prompt = true;
        if (o.banDashes)
            g.suppress_sequences = s.bans;
        if (o.cancel) {
            const std::atomic_bool *cancel = o.cancel;
            g.callback = [cancel](ctranslate2::GenerationStepResult) { return cancel->load(); };
        }
        return g;
    }

    static QString cleanAnswer(const QString &answer)
    {
        // a model that thinks aloud anyway: what stands in <think> is no answer
        static const QRegularExpression think(QStringLiteral("<think>.*?</think>"), QRegularExpression::DotMatchesEverythingOption);
        QString a = answer;
        a.remove(think);
        return a.trimmed();
    }

    QStringList chatBatch(const Model &s, const QStringList &texts, const tp::RunOptions &o)
    {
        const Tokens system = s.bpe.encode(systemPart(o));
        std::vector<Tokens> prompts;
        size_t longest = 0;
        for (const QString &t : texts) {
            prompts.push_back(s.bpe.encode(userPart(s, t)));
            longest = std::max(longest, s.bpe.encode(t).size());
        }
        ctranslate2::GenerationOptions g = chatOptions(s, o, system);
        // an answer about as long as the text; a little room for a word more
        g.max_length = std::min<size_t>(1024, longest * 2 + 32);
        auto futures = s.generator->generate_batch_async(prompts, g, prompts.size());
        QStringList out;
        for (auto &f : futures) {
            const ctranslate2::GenerationResult r = f.get();
            out << (r.sequences.empty() ? QString() : cleanAnswer(s.bpe.decode(r.sequences.front())));
        }
        return out;
    }

    QList<tp::Alternative> chatAlternatives(const Model &s, const QString &sourceText, const QString &current,
                                            int wordIndex, const tp::RunOptions &o, int count)
    {
        QList<tp::Alternative> out;
        // the answer up to the word: the words before it, as the model would have written them
        const QStringList currentWords = current.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        const QString before = currentWords.mid(0, wordIndex).join(QLatin1Char(' '));
        const Tokens system = s.bpe.encode(systemPart(o));
        Tokens prompt = s.bpe.encode(userPart(s, sourceText));
        const Tokens answerSoFar = s.bpe.encode(before);
        prompt.insert(prompt.end(), answerSoFar.begin(), answerSoFar.end());
        // step 1: the pieces that could come next (one step of the model)
        ctranslate2::GenerationOptions first = chatOptions(s, o, system);
        first.callback = nullptr;
        first.return_alternatives = true;
        first.num_hypotheses = size_t(2 * count);
        first.max_length = system.size() + prompt.size();   // counted from the start: one new piece
        const auto r1 = s.generator->generate_batch_async({prompt}, first).front().get();
        std::vector<std::string> starts;
        for (const Tokens &h : r1.sequences) {
            if (h.empty())
                continue;
            const std::string &t = h.back();
            if (t == kImEnd || t == kEndOfText || s.bpe.isSpecial(t))
                continue;
            if (std::find(starts.begin(), starts.end(), t) == starts.end())
                starts.push_back(t);
        }
        if (starts.empty())
            return out;
        if (starts.size() > size_t(count) + 2)
            starts.resize(size_t(count) + 2);
        // step 2: the rest of the answer after each of them
        std::vector<Tokens> prompts;
        for (const std::string &t : starts) {
            Tokens p = prompt;
            p.push_back(t);
            prompts.push_back(p);
        }
        ctranslate2::GenerationOptions rest = chatOptions(s, o, system);
        rest.callback = nullptr;
        rest.max_length = std::min<size_t>(512, s.bpe.encode(sourceText).size() * 2 + 32);
        auto futures = s.generator->generate_batch_async(prompts, rest, prompts.size());
        QSet<QString> seen;
        for (size_t i = 0; i < futures.size(); ++i) {
            const ctranslate2::GenerationResult r = futures[i].get();
            Tokens tail = {starts[i]};
            if (!r.sequences.empty())
                tail.insert(tail.end(), r.sequences.front().begin(), r.sequences.front().end());
            // the word: the first piece and those that go on without a space
            Tokens word;
            for (size_t k = 0; k < tail.size(); ++k) {
                if (k > 0 && (tp::BpeTokenizer::startsWord(tail[k]) || s.bpe.isSpecial(tail[k])))
                    break;
                word.push_back(tail[k]);
            }
            const QString w = s.bpe.decode(word).trimmed();
            if (w.isEmpty() || seen.contains(w))
                continue;
            seen.insert(w);
            const QString sentence = (before.isEmpty() ? QString() : before + QLatin1Char(' '))
                                     + cleanAnswer(s.bpe.decode(tail));
            out << tp::Alternative{w, sentence.simplified()};
        }
        return out;
    }

    // Every piece of the vocabulary with a dash between words (em dash, or an en dash after a space): the model may
    // not write one (rule "no em-dashes"). A hyphen in a word stays allowed.
    static std::vector<std::vector<std::string>> chatDashBans(const tp::BpeTokenizer &bpe, const QString &dir)
    {
        const std::string em = tp::BpeTokenizer::byteLevel(QStringLiteral("—").toUtf8());
        const std::string spacedEn = tp::BpeTokenizer::byteLevel(QStringLiteral(" –").toUtf8());
        std::vector<std::vector<std::string>> out;
        QFile f(dir + QStringLiteral("/vocabulary.json"));
        if (f.open(QIODevice::ReadOnly)) {
            for (const QJsonValue &v : QJsonDocument::fromJson(f.readAll()).array()) {
                const std::string t = ss(v.toString());
                if (!bpe.isSpecial(t) && (t.find(em) != std::string::npos || t.find(spacedEn) != std::string::npos))
                    out.push_back({t});
            }
        }
        return out;
    }

    mutable QMutex m_mutex;
    std::map<QString, std::shared_ptr<Model>> m_slots;
};

} // namespace

extern "C" Q_DECL_EXPORT tp::EngineInterface *qb_tp_engine_create()
{
    return new Engine;
}
