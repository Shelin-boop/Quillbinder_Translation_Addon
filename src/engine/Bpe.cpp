#include "Bpe.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>
#include <array>
#include <climits>

namespace tp {

namespace {

// GPT-2's map of the 256 byte values onto printable characters (bytes_to_unicode): the visible ASCII and Latin-1
// characters stand for themselves, the others (space, control characters ...) for the code points from U+0100 on.
struct ByteMap {
    std::array<std::string, 256> toChar;
    std::unordered_map<uint32_t, uint8_t> toByte;
    ByteMap()
    {
        std::array<bool, 256> direct{};
        for (int b = '!'; b <= '~'; ++b)
            direct[size_t(b)] = true;
        for (int b = 0xA1; b <= 0xAC; ++b)
            direct[size_t(b)] = true;
        for (int b = 0xAE; b <= 0xFF; ++b)
            direct[size_t(b)] = true;
        int n = 0;
        for (int b = 0; b < 256; ++b) {
            const uint32_t cp = direct[size_t(b)] ? uint32_t(b) : uint32_t(256 + n++);
            toChar[size_t(b)] = QString(QChar(char16_t(cp))).toUtf8().toStdString();
            toByte[cp] = uint8_t(b);
        }
    }
};

const ByteMap &byteMap()
{
    static const ByteMap m;
    return m;
}

// the UTF-8 characters of a string, one string each
std::vector<std::string> utf8Chars(const std::string &s)
{
    std::vector<std::string> out;
    for (size_t i = 0; i < s.size();) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        const size_t n = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : (c >> 3) == 0x1E ? 4 : 1;
        out.push_back(s.substr(i, n));
        i += n;
    }
    return out;
}

// GPT-2's pattern, used when the tokenizer names none of its own
const char *kGpt2Pattern = "'s|'t|'re|'ve|'m|'ll|'d| ?\\p{L}+| ?\\p{N}+| ?[^\\s\\p{L}\\p{N}]+|\\s+(?!\\S)|\\s+";

QString findSplitPattern(const QJsonObject &pre)
{
    const QString type = pre.value(QStringLiteral("type")).toString();
    if (type == QLatin1String("Split"))
        return pre.value(QStringLiteral("pattern")).toObject().value(QStringLiteral("Regex")).toString();
    if (type == QLatin1String("Sequence"))
        for (const QJsonValue &v : pre.value(QStringLiteral("pretokenizers")).toArray()) {
            const QString p = findSplitPattern(v.toObject());
            if (!p.isEmpty())
                return p;
        }
    if (type == QLatin1String("ByteLevel") && pre.value(QStringLiteral("use_regex")).toBool(true))
        return QString::fromLatin1(kGpt2Pattern);
    return QString();
}

bool hasNfc(const QJsonObject &norm)
{
    const QString type = norm.value(QStringLiteral("type")).toString();
    if (type == QLatin1String("NFC"))
        return true;
    if (type == QLatin1String("Sequence"))
        for (const QJsonValue &v : norm.value(QStringLiteral("normalizers")).toArray())
            if (hasNfc(v.toObject()))
                return true;
    return false;
}

} // namespace

std::string BpeTokenizer::byteLevel(const QByteArray &utf8)
{
    std::string out;
    for (char c : utf8)
        out += byteMap().toChar[size_t(static_cast<unsigned char>(c))];
    return out;
}

bool BpeTokenizer::startsWord(const std::string &piece)
{
    // "Ġ" (a space) or "Ċ" (a line break) in the byte-level form
    return piece.rfind("\xc4\xa0", 0) == 0 || piece.rfind("\xc4\x8a", 0) == 0;
}

bool BpeTokenizer::load(const QString &tokenizerJson, QString *error)
{
    QFile f(tokenizerJson);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QStringLiteral("cannot read %1").arg(tokenizerJson);
        return false;
    }
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    const QJsonObject model = root.value(QStringLiteral("model")).toObject();
    if (model.value(QStringLiteral("type")).toString() != QLatin1String("BPE")) {
        if (error)
            *error = QStringLiteral("%1 is no BPE tokenizer").arg(tokenizerJson);
        return false;
    }
    m_ranks.clear();
    m_vocab.clear();
    m_special.clear();
    m_specialByLength.clear();
    m_cache.clear();
    const QJsonObject vocab = model.value(QStringLiteral("vocab")).toObject();
    for (auto it = vocab.begin(); it != vocab.end(); ++it)
        m_vocab.insert(it.key().toStdString());
    int rank = 0;
    for (const QJsonValue &v : model.value(QStringLiteral("merges")).toArray()) {
        std::string key;
        if (v.isString()) {
            key = v.toString().toStdString();
        } else {
            const QJsonArray pair = v.toArray();
            key = pair.at(0).toString().toStdString() + ' ' + pair.at(1).toString().toStdString();
        }
        m_ranks.emplace(key, rank++);
    }
    m_ignoreMerges = model.value(QStringLiteral("ignore_merges")).toBool(false);
    m_nfc = hasNfc(root.value(QStringLiteral("normalizer")).toObject());
    QString pattern = findSplitPattern(root.value(QStringLiteral("pre_tokenizer")).toObject());
    if (pattern.isEmpty())
        pattern = QString::fromLatin1(kGpt2Pattern);
    m_split = QRegularExpression(pattern, QRegularExpression::UseUnicodePropertiesOption);
    if (!m_split.isValid()) {
        if (error)
            *error = QStringLiteral("the tokenizer's pattern cannot be used: %1").arg(m_split.errorString());
        m_ranks.clear();
        return false;
    }
    for (const QJsonValue &v : root.value(QStringLiteral("added_tokens")).toArray()) {
        const QString content = v.toObject().value(QStringLiteral("content")).toString();
        if (!content.isEmpty()) {
            m_special.insert(content.toStdString());
            m_specialByLength << content;
        }
    }
    std::sort(m_specialByLength.begin(), m_specialByLength.end(),
              [](const QString &a, const QString &b) { return a.size() > b.size(); });
    if (m_ranks.empty()) {
        if (error)
            *error = QStringLiteral("%1 has no merges").arg(tokenizerJson);
        return false;
    }
    return true;
}

std::vector<std::string> BpeTokenizer::encode(const QString &text) const
{
    std::vector<std::string> out;
    int start = 0;
    int i = 0;
    while (i < text.size()) {
        QString found;
        if (text.at(i) == QLatin1Char('<'))   // every special token of these models starts with "<"
            for (const QString &s : m_specialByLength)
                if (QStringView(text).mid(i).startsWith(s)) {
                    found = s;
                    break;
                }
        if (found.isEmpty()) {
            ++i;
            continue;
        }
        if (i > start) {
            const std::vector<std::string> plain = encodePlain(text.mid(start, i - start));
            out.insert(out.end(), plain.begin(), plain.end());
        }
        out.push_back(found.toStdString());
        i += int(found.size());
        start = i;
    }
    if (start < text.size()) {
        const std::vector<std::string> plain = encodePlain(text.mid(start));
        out.insert(out.end(), plain.begin(), plain.end());
    }
    return out;
}

std::vector<std::string> BpeTokenizer::encodePlain(const QString &raw) const
{
    const QString text = m_nfc ? raw.normalized(QString::NormalizationForm_C) : raw;
    std::vector<std::string> out;
    auto add = [&](const QString &piece) {
        if (piece.isEmpty())
            return;
        const std::vector<std::string> parts = bpe(byteLevel(piece.toUtf8()));
        out.insert(out.end(), parts.begin(), parts.end());
    };
    int last = 0;
    for (auto it = m_split.globalMatch(text); it.hasNext();) {
        const QRegularExpressionMatch m = it.next();
        if (m.capturedLength() == 0)
            continue;
        add(text.mid(last, m.capturedStart() - last));   // what the pattern left between two matches
        add(m.captured(0));
        last = int(m.capturedEnd());
    }
    add(text.mid(last));
    return out;
}

std::vector<std::string> BpeTokenizer::bpe(const std::string &word) const
{
    if (const auto c = m_cache.find(word); c != m_cache.end())
        return c->second;
    if (m_ignoreMerges && m_vocab.count(word))
        return {word};
    std::vector<std::string> parts = utf8Chars(word);
    while (parts.size() > 1) {
        int best = INT_MAX;
        size_t at = 0;
        for (size_t i = 0; i + 1 < parts.size(); ++i) {
            const auto r = m_ranks.find(parts[i] + ' ' + parts[i + 1]);
            if (r != m_ranks.end() && r->second < best) {
                best = r->second;
                at = i;
            }
        }
        if (best == INT_MAX)
            break;
        const std::string left = parts[at], right = parts[at + 1];
        std::vector<std::string> merged;
        merged.reserve(parts.size());
        for (size_t i = 0; i < parts.size();) {
            if (i + 1 < parts.size() && parts[i] == left && parts[i + 1] == right) {
                merged.push_back(left + right);
                i += 2;
            } else {
                merged.push_back(parts[i]);
                ++i;
            }
        }
        parts.swap(merged);
    }
    if (m_cache.size() > 50000)
        m_cache.clear();
    m_cache.emplace(word, parts);
    return parts;
}

QString BpeTokenizer::decode(const std::vector<std::string> &pieces) const
{
    std::string joined;
    for (const std::string &p : pieces)
        if (!isSpecial(p))
            joined += p;
    QByteArray bytes;
    const QString s = QString::fromStdString(joined);
    const auto &map = byteMap().toByte;
    for (QChar ch : s) {
        const auto it = map.find(ch.unicode());
        if (it != map.end())
            bytes.append(char(it->second));
        else
            bytes.append(QString(ch).toUtf8());
    }
    return QString::fromUtf8(bytes);
}

} // namespace tp
