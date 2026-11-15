#!/usr/bin/env python3
"""Writes resources/en-thesaurus.tsv: words of a similar meaning for English words (1.1: the information area and a
single word in the input), and the irregular forms that lead back to them.

    python3 tools/make-thesaurus.py [WordNet folder]     (default: /usr/share/wordnet, Ubuntu package wordnet-base)

Made from Princeton WordNet 3.0 (license: resources/en-thesaurus-LICENSE.txt). For every word: the other words of
its senses, most frequent sense first (WordNet's index order), by part of speech; for adjectives also the words of
the senses WordNet marks as similar. Lines:

    word<TAB>n:first;second|v:...|a:...|r:...      (n noun, v verb, a adjective, r adverb)
    @inflected<TAB>base[ base2]                     (irregular forms: "went" -> "go"; the regular ones are rules)
"""
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
POS = [("noun", "n"), ("verb", "v"), ("adj", "a"), ("adv", "r")]
PER_POS = 16   # words kept per part of speech


def clean(w):
    w = w.replace("_", " ")
    # adjective markers: "big(a)", "galore(ip)"
    if w.endswith(")") and "(" in w:
        w = w[: w.rindex("(")]
    return w


def read_data(path):
    """offset -> (words, similar-to offsets)"""
    out = {}
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            if line.startswith("  "):
                continue
            head = line.split(" | ", 1)[0].split()
            offset, cnt = head[0], int(head[3], 16)
            words = [clean(head[4 + 2 * i]) for i in range(cnt)]
            i = 4 + 2 * cnt
            pcnt = int(head[i])
            similar = []
            for p in range(pcnt):
                sym, off, pos = head[i + 1 + 4 * p], head[i + 2 + 4 * p], head[i + 3 + 4 * p]
                if sym == "&" and pos in ("a", "s"):
                    similar.append(off)
            out[offset] = (words, similar)
    return out


def main():
    wn = sys.argv[1] if len(sys.argv) > 1 else "/usr/share/wordnet"
    table = {}
    for name, tag in POS:
        data = read_data(os.path.join(wn, f"data.{name}"))
        with open(os.path.join(wn, f"index.{name}"), encoding="utf-8", errors="replace") as f:
            for line in f:
                if line.startswith("  "):
                    continue
                parts = line.split()
                lemma = clean(parts[0])
                synsets = parts[-int(parts[2]):]
                syns = []
                for off in synsets:
                    words, similar = data.get(off, ([], []))
                    group = list(words)
                    for s in similar:
                        group += data.get(s, ([], []))[0]
                    for w in group:
                        if w.lower() != lemma.lower() and w not in syns:
                            syns.append(w)
                if syns:
                    table.setdefault(lemma, []).append(f"{tag}:" + ";".join(syns[:PER_POS]))
    exceptions = {}
    for name, _ in POS:
        with open(os.path.join(wn, f"{name}.exc"), encoding="utf-8", errors="replace") as f:
            for line in f:
                p = line.split()
                if len(p) >= 2:
                    exceptions.setdefault(clean(p[0]), [])
                    for b in p[1:]:
                        if clean(b) not in exceptions[clean(p[0])]:
                            exceptions[clean(p[0])].append(clean(b))
    out = os.path.join(ROOT, "resources", "en-thesaurus.tsv")
    with open(out, "w", encoding="utf-8") as f:
        f.write("# Words of a similar meaning, from Princeton WordNet 3.0 (resources/en-thesaurus-LICENSE.txt); "
                "made by tools/make-thesaurus.py\n")
        for lemma in sorted(table):
            f.write(lemma + "\t" + "|".join(table[lemma]) + "\n")
        for w in sorted(exceptions):
            f.write("@" + w + "\t" + " ".join(exceptions[w]) + "\n")
    print(f"{out}: {len(table)} words, {len(exceptions)} irregular forms, {os.path.getsize(out)} bytes")


if __name__ == "__main__":
    main()
