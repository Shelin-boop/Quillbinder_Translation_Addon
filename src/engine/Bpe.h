#pragma once
// The tokenizer of the chat models (1.1): byte-level BPE as described by a Hugging Face tokenizer.json (the Qwen
// models'): special tokens are cut out first, the rest is normalized (NFC), split by the pre-tokenizer's pattern,
// mapped byte by byte onto printable characters and merged by the ranks of the merge list. Qt Core only, so that
// tests/logic_tests.cpp can check it against the pieces the model's own tokenizer gave (tokenizer-check.json).
#include <QHash>
#include <QRegularExpression>
#include <QString>
#include <QStringList>

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace tp {

class BpeTokenizer
{
public:
    bool load(const QString &tokenizerJson, QString *error);
    bool isLoaded() const { return !m_ranks.empty(); }

    // the pieces of a text; special tokens written in it ("<|im_start|>") stay one piece each
    std::vector<std::string> encode(const QString &text) const;
    // the text of pieces; special tokens are left out
    QString decode(const std::vector<std::string> &pieces) const;

    bool isSpecial(const std::string &piece) const { return m_special.count(piece) > 0; }
    bool hasPiece(const std::string &piece) const { return m_vocab.count(piece) > 0; }
    // a piece that begins a new word: it starts with a space or a line break (in the byte-level form)
    static bool startsWord(const std::string &piece);
    // the byte-level form of a text ("—" becomes "âĢĶ"), for pieces the engine must know by name
    static std::string byteLevel(const QByteArray &utf8);

private:
    std::vector<std::string> encodePlain(const QString &text) const;
    std::vector<std::string> bpe(const std::string &word) const;

    QRegularExpression m_split;
    bool m_nfc = false;
    bool m_ignoreMerges = false;
    std::unordered_map<std::string, int> m_ranks;   // "left right" -> rank
    std::unordered_set<std::string> m_vocab;
    std::unordered_set<std::string> m_special;
    QStringList m_specialByLength;                  // longest first, to cut them out of a text
    mutable std::unordered_map<std::string, std::vector<std::string>> m_cache;
};

} // namespace tp
