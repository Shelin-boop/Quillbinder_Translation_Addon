#!/usr/bin/env python3
"""Converts the add-on's models for the engine (CTranslate2, int8) and writes their catalog.

    python3 models/convert.py <out dir>

Run by .github/workflows/models.yml on GitHub's runner; the files go onto the release "models-1" of this
repository, from where the add-on downloads the model the writer chooses. Every file is named
"<model id>--<file name>" there; models/catalog.json (written into <out dir> as well) lists them with
size and SHA-256, and the add-on checks each download against it.

Needs: ctranslate2 4.9.0, transformers 4.x, torch (CPU), sentencepiece, huggingface_hub.
"""
import hashlib
import json
import os
import shutil
import subprocess
import sys

from huggingface_hub import hf_hub_download

CT2_VERSION = "4.9.0"

# Each model: the original on Hugging Face at a fixed revision, how it is used, its licence.
MODELS = [
    {
        "id": "coedit-large",
        "kind": "corrector",
        "title": "CoEdIT Large (Grammarly)",
        "summary": "Best quality: finds most spelling and grammar errors. Non-commercial use only.",
        "repo": "grammarly/coedit-large",
        "revision": "5637bcdf9d8d4419f97c8cfea36f7d35c79232b6",
        "tokenizer": {"type": "t5", "repo": "grammarly/coedit-large",
                      "revision": "5637bcdf9d8d4419f97c8cfea36f7d35c79232b6", "file": "spiece.model"},
        "license": "CC BY-NC 4.0",
        "licenseUrl": "https://creativecommons.org/licenses/by-nc/4.0/",
        "nonCommercial": True,
        "credit": "CoEdIT-large by Vipul Raheja, Dhruv Kumar, Ryan Koo and Dongyeop Kang (Grammarly), "
                  "https://huggingface.co/grammarly/coedit-large, licensed under CC BY-NC 4.0. "
                  "Changed: converted to CTranslate2 with 8-bit weights.",
        "memoryMB": 1600,
    },
    {
        "id": "coedit-base",
        "kind": "corrector",
        "title": "CoEdIT Base (free licence)",
        "summary": "Smaller and faster, free for any use, but it misses many errors.",
        "repo": "jbochi/coedit-base",
        "revision": "bef93f1b9bae24f9383580764820d5de1adafcdc",
        # the model has only tokenizer.json; its vocabulary is FLAN-T5's, whose SentencePiece file is used
        # (checked below: both give the same pieces)
        "tokenizer": {"type": "t5", "repo": "google/flan-t5-base",
                      "revision": "7bcac572ce56db69c1ea7c8af255c5d7c9672fc2", "file": "spiece.model"},
        "license": "Apache-2.0",
        "licenseUrl": "https://www.apache.org/licenses/LICENSE-2.0",
        "nonCommercial": False,
        "credit": "coedit-base by J. Bochi, https://huggingface.co/jbochi/coedit-base (FLAN-T5 base trained on "
                  "Grammarly's CoEdIT data), licensed under Apache-2.0. Changed: converted to CTranslate2 with "
                  "8-bit weights.",
        "memoryMB": 600,
    },
    {
        "id": "opus-mt-de-en",
        "kind": "translator",
        "title": "OPUS-MT German to English (University of Helsinki)",
        "summary": "Translates German into English. Free for any use.",
        "repo": "Helsinki-NLP/opus-mt-de-en",
        "revision": "1a922f3b32a8e809e17a47d4b32142d8105924e5",
        "tokenizer": {"type": "marian", "repo": "Helsinki-NLP/opus-mt-de-en",
                      "revision": "1a922f3b32a8e809e17a47d4b32142d8105924e5",
                      "source": "source.spm", "target": "target.spm"},
        "license": "Apache-2.0",
        "licenseUrl": "https://www.apache.org/licenses/LICENSE-2.0",
        "nonCommercial": False,
        "credit": "opus-mt-de-en by Language Technology Research Group at the University of Helsinki "
                  "(Helsinki-NLP), https://huggingface.co/Helsinki-NLP/opus-mt-de-en, licensed under Apache-2.0. "
                  "Changed: converted to CTranslate2 with 8-bit weights.",
        "memoryMB": 300,
    },
]

SAMPLES = [
    "The rain had not stopped for three days, and Mara was starting to loose her patience.",
    "“I dont think he is coming,” she said quietly — more to herself then to anyone else.",
    "Der Regen hatte seit drei Tagen nicht aufgehört; „Ich glaube nicht, dass er kommt“, sagte sie.",
]


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()


def check_t5_tokenizer(m, spm_file):
    """The SentencePiece file must split text exactly as the model's own tokenizer does."""
    import sentencepiece as spm
    from transformers import AutoTokenizer
    sp = spm.SentencePieceProcessor(model_file=spm_file)
    tok = AutoTokenizer.from_pretrained(m["repo"], revision=m["revision"])
    for s in SAMPLES:
        a = sp.encode(s, out_type=str)
        b = tok.convert_ids_to_tokens(tok.encode(s, add_special_tokens=False))
        if a != b:
            sys.exit(f"{m['id']}: tokenizer differs for {s!r}:\n  {a}\n  {b}")


def main():
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    work = os.path.join(out, ".work")
    catalog = {"format": 1, "release": "models-1", "engine": f"CTranslate2 {CT2_VERSION}", "models": []}
    for m in MODELS:
        d = os.path.join(work, m["id"])
        shutil.rmtree(d, ignore_errors=True)
        subprocess.run(["ct2-transformers-converter", "--model", m["repo"], "--revision", m["revision"],
                        "--output_dir", d, "--quantization", "int8", "--force"], check=True)
        t = m["tokenizer"]
        names = ["source", "target"] if t["type"] == "marian" else ["file"]
        tok_files = {}
        for n in names:
            src = hf_hub_download(t["repo"], t[n], revision=t["revision"])
            dst = os.path.join(d, t[n])
            shutil.copy(src, dst)
            tok_files[n] = t[n]
        if t["type"] == "t5":
            check_t5_tokenizer(m, os.path.join(d, t["file"]))
        with open(os.path.join(d, "MODEL-LICENSE.txt"), "w", encoding="utf-8") as f:
            f.write(f"{m['title']}\n\n{m['credit']}\n\nLicence: {m['license']} ({m['licenseUrl']})\n"
                    f"Original: https://huggingface.co/{m['repo']} at revision {m['revision']}\n")
        files = []
        for name in sorted(os.listdir(d)):
            p = os.path.join(d, name)
            asset = f"{m['id']}--{name}"
            shutil.copy(p, os.path.join(out, asset))
            files.append({"name": name, "asset": asset, "size": os.path.getsize(p), "sha256": sha256(p)})
        entry = {k: m[k] for k in ("id", "kind", "title", "summary", "license", "licenseUrl", "nonCommercial",
                                   "credit", "memoryMB")}
        entry["source"] = f"https://huggingface.co/{m['repo']}/tree/{m['revision']}"
        entry["tokenizer"] = {"type": t["type"], **tok_files}
        entry["files"] = files
        entry["sizeMB"] = round(sum(f["size"] for f in files) / 1e6)
        catalog["models"].append(entry)
        print(f"{m['id']}: {entry['sizeMB']} MB, {len(files)} files")
    shutil.rmtree(work, ignore_errors=True)
    with open(os.path.join(out, "catalog.json"), "w", encoding="utf-8") as f:
        json.dump(catalog, f, indent=2, ensure_ascii=False)
        f.write("\n")


if __name__ == "__main__":
    main()
