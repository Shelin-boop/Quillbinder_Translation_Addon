#pragma once
// Between the two libraries of the add-on "Translation and Proofreading":
//  - libqb-translation-proofreading.so  the writing tool the program loads (Qt Widgets, Network): commands, the
//                                       panel in the inspector and in its own window, options, model downloads
//  - libqb-tp-engine.so                 the engine, loaded by the tool when it is first needed: CTranslate2 with
//                                       oneDNN and SentencePiece, linked in, and the chat models' BPE tokenizer
//                                       (Bpe.cpp); it needs only Qt Core and libgomp
// The tool finds the engine in the add-on's folder "translation-proofreading" and calls qb_tp_engine_create().
// Both are built from this source together, so this header may change with them.
#include <QList>
#include <QString>
#include <QStringList>

#include <atomic>
#include <functional>

namespace tp {

// A model on disk (a folder with model.bin, config.json, the vocabulary and the tokenizer's files).
struct ModelFiles {
    QString id;             // catalog id, "coedit-large"
    QString dir;
    QString type;           // "seq2seq" (CoEdIT, OPUS-MT: text in, text out) or "chat" (1.1: Qwen, told what to do)
    QString tokenizer;      // "t5" (one SentencePiece file), "marian" (one for each side) or "bpe" (tokenizer.json)
    QString spm;            // t5: the file name in dir; bpe: the tokenizer.json
    QString spmSource;      // marian
    QString spmTarget;      // marian
    QString sourceToken;    // marian, several target languages: the token put before the text (">>eng<<")
    bool thinkTags = false; // chat: the model's prompt ends with an empty "<think></think>" (Qwen3 without thinking)
};

struct RunOptions {
    QString prefix;         // put before every text (the instruction of a T5 model: "Fix grammatical errors ...")
    QString system;         // chat models: what the model is told to do (the system message)
    // chat models: earlier turns that show the work (what the writer gave, what the model answered, ...); with the
    // system message they are the part of the prompt the model works through once
    QStringList examples;
    int beam = 2;
    bool banDashes = true;  // the model may not write a dash between words
    int batch = 8;          // texts worked together (faster, more memory)
    // chat models: set by the caller to stop a text in the middle (live work: the writer typed on); may be null
    const std::atomic_bool *cancel = nullptr;
};

struct Alternative {
    QString word;           // the word that would stand at the place
    QString sentence;       // the whole sentence with it, as the model goes on from there
};

class EngineInterface
{
public:
    virtual ~EngineInterface() = default;
    // slot: "corrector" or "translator"; loads the model there (the one loaded before is freed). threads 0: all.
    // A model loaded in the other slot already is shared, not loaded twice (1.1: a chat model does both).
    virtual bool load(const QString &slot, const ModelFiles &model, int threads, QString *error) = 0;
    virtual void unload(const QString &slot) = 0;
    virtual QString loaded(const QString &slot) const = 0;   // the model id, empty: none
    // Works through the texts with the model of the slot; blocks (call it outside the main thread).
    // progress(done, total) after each batch; returning false stops (the rest comes back empty).
    virtual QStringList run(const QString &slot, const QStringList &texts, const RunOptions &options,
                            const std::function<bool(int, int)> &progress, QString *error) = 0;
    // Other words for the word number wordIndex (counted at spaces) of `current`, which the model wrote (or the
    // writer chose) for `source`; each with the sentence as the model goes on after it. Blocks.
    virtual QList<Alternative> alternatives(const QString &slot, const QString &source, const QString &current,
                                            int wordIndex, const RunOptions &options, int count, QString *error) = 0;
    virtual QString version() const = 0;
};

} // namespace tp

using TpEngineCreateFn = tp::EngineInterface *(*)();
// extern "C" tp::EngineInterface *qb_tp_engine_create();
