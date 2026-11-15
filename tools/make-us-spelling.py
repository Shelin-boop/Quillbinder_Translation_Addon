#!/usr/bin/env python3
"""Writes resources/us-spelling.tsv (British spellings and their American form, one pair per line) and
resources/en-words.txt (the American dictionary's words, to judge the other words a model offers).

    python3 tools/make-us-spelling.py [en_US.dic en_US.aff en_GB.dic en_GB.aff]

Made from the American and British Hunspell dictionaries of SCOWL (Kevin Atkinson; the Ubuntu packages
hunspell-en-us and hunspell-en-gb, both under SCOWL's permissive licence, see resources/us-spelling-LICENSE.txt):
every word the British dictionary has and the American one does not is changed by the usual rules (-our to
-or, -ise to -ize, -tre to -ter, a doubled l, -ence to -ense, ...); the pair is kept only when the American
dictionary has the result. A short list of our own adds the common words both dictionaries accept but that
American writing avoids (towards, amongst, grey ...). Needs `unmunch` (Ubuntu package hunspell-tools).
"""
import os
import re
import subprocess
import sys

H = "/usr/share/hunspell"
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Words both dictionaries know, but American usage prefers the second (written for this add-on).
OWN = """
towards toward
afterwards afterward
backwards backward
forwards forward
upwards upward
downwards downward
amongst among
whilst while
learnt learned
kerb curb
kerbs curbs
tyre tire
tyres tires
storey story
storeys stories
grey gray
greys grays
greyer grayer
greyest grayest
greyish grayish
greying graying
greyed grayed
greyness grayness
aluminium aluminum
plough plow
ploughs plows
ploughed plowed
ploughing plowing
moustache mustache
moustaches mustaches
pyjamas pajamas
cosy cozy
cosier cozier
cosiest coziest
sceptic skeptic
sceptics skeptics
sceptical skeptical
scepticism skepticism
mould mold
moulds molds
mouldy moldy
smoulder smolder
smouldered smoldered
smouldering smoldering
manoeuvre maneuver
manoeuvres maneuvers
manoeuvred maneuvered
manoeuvring maneuvering
artefact artifact
artefacts artifacts
gaol jail
gaols jails
programme program
programmes programs
judgement judgment
judgements judgments
ageing aging
""".split()

# (pattern, replacement) tried one place at a time; the patterns are regular expressions, most of them tied to the
# end of a word, where the British and American forms differ (a looser rule turned "prise" into "prize")
RULES = [
    (r"our", "or"), (r"is(?=(e|es|ed|ing|er|ers|ation|ations|able|m|ms|t|ts)$)", "iz"),
    (r"ys(?=(e|es|ed|ing|er|ers)$)", "yz"), (r"tre(?=(s|d)?$)", "ter"), (r"tred$", "tered"), (r"bre(?=s?$)", "ber"),
    (r"ll(?=(ed|ing|er|ers|or|ors)$)", "l"), (r"ence(?=s?$)", "ense"),
    # ae, oe inside a word only (anaemia, foetus); at the end they are Latin plurals (fossae, salvoes)
    (r"(?<=.)ae(?=[a-z]{2})", "e"), (r"(?<=.)oe(?=[a-z]{2})", "e"),
    (r"mme(?=s?$)", "m"), (r"dgement", "dgment"), (r"eing$", "ing"), (r"ogue(?=s?$)", "og"), (r"que(?=s?$)", "ck"),
]
# results that are wrong for American writing although the dictionary has them
NEVER = {"dialog", "dialogs", "analog"}
# British words the rules would change wrongly (another word, not another spelling)
NEVER_SOURCE = {"prise", "prised", "prises", "prising", "pourer", "pourers", "hommes", "previse", "previses",
                "baller", "ballers", "poller", "pollers", "taxaer", "taxaers"}


def words(dic, aff):
    out = subprocess.run(["unmunch", dic, aff], capture_output=True, text=True, errors="replace").stdout
    return {w.strip() for w in out.splitlines() if re.fullmatch(r"[A-Za-z]+", w.strip())}


def candidates(w):
    for a, b in RULES:
        ms = list(re.finditer(a, w))
        for m in ms:
            yield w[:m.start()] + b + w[m.end():]
        if len(ms) > 1:
            yield re.sub(a, b, w)


def main():
    args = sys.argv[1:] or [f"{H}/en_US.dic", f"{H}/en_US.aff", f"{H}/en_GB.dic", f"{H}/en_GB.aff"]
    us = words(args[0], args[1])
    gb = words(args[2], args[3])
    us_l = {w.lower() for w in us}
    pairs = {}
    for w in sorted(gb):
        if w != w.lower() or w in us_l or len(w) < 6 or w in NEVER_SOURCE:
            continue
        found = [c for c in candidates(w) if c in us_l and c not in NEVER]
        found = list(dict.fromkeys(found))
        if len(found) == 1:
            pairs[w] = found[0]
    # "-ller(s)" only where the verb itself is in the list (traveller with travelled; not baller)
    for w in [w for w in pairs if re.search(r"llers?$", w)]:
        if re.sub(r"llers?$", "lled", w) not in pairs:
            del pairs[w]
    for a, b in zip(OWN[0::2], OWN[1::2]):
        pairs[a] = b
    path = os.path.join(ROOT, "resources", "us-spelling.tsv")
    with open(path, "w", encoding="utf-8") as f:
        f.write("# British spelling<TAB>American spelling. Made by tools/make-us-spelling.py from SCOWL's Hunspell\n"
                "# dictionaries (see us-spelling-LICENSE.txt) and a short list of the add-on's own.\n")
        for a in sorted(pairs):
            f.write(f"{a}\t{pairs[a]}\n")
    print(f"{path}: {len(pairs)} pairs (American dictionary {len(us)} words, British {len(gb)})")
    # the American words, to tell real words from invented ones among the models' other words
    words_path = os.path.join(ROOT, "resources", "en-words.txt")
    with open(words_path, "w", encoding="utf-8") as f:
        for w in sorted(us):
            f.write(w + "\n")
    print(f"{words_path}: {len(us)} words")


if __name__ == "__main__":
    main()
