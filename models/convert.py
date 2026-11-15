#!/usr/bin/env python3
"""Converts the add-on's models for the engine (CTranslate2, int8) and writes their catalog.

    python3 models/convert.py <out dir>

Run by .github/workflows/models.yml on GitHub's runner; the files go onto the release named in RELEASE, from where
the add-on downloads the model the writer chooses. Every file is named "<model id>--<file name>" there; a file
larger than GitHub allows for one release file is cut into parts ("...--model.bin.part1", ...), which the add-on
joins after checking each. The catalog (written into <out dir> as catalog.json, copied into resources/catalog.json)
lists the models of every release with size and SHA-256 of each file; the add-on checks each download against it.
A published release is never changed: the models of "models-1" (1.0) are taken over from resources/catalog.json as
they are, the new ones of this run go onto "models-2" (1.1).

Needs: ctranslate2 4.9.0, transformers 4.51 or later, torch (CPU), sentencepiece, huggingface_hub.
"""
import hashlib
import json
import os
import shutil
import subprocess
import sys

from huggingface_hub import hf_hub_download

CT2_VERSION = "4.9.0"
RELEASE = "models-2"
PART = 1_900_000_000   # GitHub takes files up to 2 GiB on a release

# The models of "models-1" (1.0), converted then; kept here as the record of how (not converted again).
MODELS_1 = [
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
        "title": "CoEdIT Base (free license)",
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

# The models of "models-2" (1.1). "revision": None takes the newest revision at conversion time and records it.
MODELS = [
    {
        "id": "qwen3-1.7b",
        "kinds": ["corrector", "translator"],
        "type": "chat",
        "title": "Qwen3 1.7B (Qwen team, Alibaba Cloud)",
        "summary": "A small general model told to fix only real errors and to leave your style and dialogue alone; "
                   "it also translates German. Free for any use.",
        "repo": "Qwen/Qwen3-1.7B",
        "revision": None,
        "chat": {"template": "chatml", "thinkTags": True},
        "license": "Apache-2.0",
        "licenseUrl": "https://www.apache.org/licenses/LICENSE-2.0",
        "nonCommercial": False,
        "credit": "Qwen3-1.7B by the Qwen team (Alibaba Cloud), https://huggingface.co/Qwen/Qwen3-1.7B, licensed under "
                  "Apache-2.0. Changed: converted to CTranslate2 with 8-bit weights.",
    },
    {
        "id": "qwen3-4b",
        "kinds": ["corrector", "translator"],
        "type": "chat",
        "title": "Qwen3 4B (Qwen team, Alibaba Cloud)",
        "summary": "A larger general model told to fix only real errors and to leave your style and dialogue alone; it "
                   "also translates German. Better but slower; needs a computer with 8 GB of memory or more. Free "
                   "for any use.",
        "repo": "Qwen/Qwen3-4B-Instruct-2507",
        "revision": None,
        "chat": {"template": "chatml", "thinkTags": False},
        "license": "Apache-2.0",
        "licenseUrl": "https://www.apache.org/licenses/LICENSE-2.0",
        "nonCommercial": False,
        "credit": "Qwen3-4B-Instruct-2507 by the Qwen team (Alibaba Cloud), "
                  "https://huggingface.co/Qwen/Qwen3-4B-Instruct-2507, licensed under Apache-2.0. Changed: converted "
                  "to CTranslate2 with 8-bit weights.",
    },
    {
        "id": "opus-mt-big-de-en",
        "kinds": ["translator"],
        "type": "seq2seq",
        "title": "OPUS-MT Big, German to English (University of Helsinki)",
        "summary": "A larger translation model than OPUS-MT German to English; usually translates better. Free for any "
                   "use with credit (CC BY 4.0).",
        "repo": "Helsinki-NLP/opus-mt-tc-big-gmw-gmw",
        "revision": None,
        "tokenizer": {"type": "marian", "source": "source.spm", "target": "target.spm"},
        "sourceToken": ">>eng<<",
        "license": "CC BY 4.0",
        "licenseUrl": "https://creativecommons.org/licenses/by/4.0/",
        "nonCommercial": False,
        "credit": "opus-mt-tc-big-gmw-gmw by the Language Technology Research Group at the University of Helsinki "
                  "(Helsinki-NLP), https://huggingface.co/Helsinki-NLP/opus-mt-tc-big-gmw-gmw, licensed under CC BY "
                  "4.0. Changed: converted to CTranslate2 with 8-bit weights; used for German to English only.",
    },
]

SAMPLES = [
    "The rain had not stopped for three days, and Mara was starting to loose her patience.",
    "“I dont think he is coming,” she said quietly — more to herself then to anyone else.",
    "Der Regen hatte seit drei Tagen nicht aufgehört; „Ich glaube nicht, dass er kommt“, sagte sie.",
    "“You ever been to the sea?” she asked. “Once. It were cold.”",
    "\n\n  Two spaces, a tab\tand <tags> & numbers 12345 – done.",
]

# What a chat model is told (the add-on's own instructions are in src/Panel.cpp; these are for the preview only).
PREVIEW_SYSTEM = ("You are a careful proofreader of English fiction. Correct only clear errors of spelling, grammar "
                  "and punctuation. Keep the author's words, style and dialogue. Answer with the corrected text only.")
PREVIEW = [
    "She turned around and looked at him. There was not much to loose.",
    "“Well that's easy. First, you need to find a river. Then if would be helpful if it could even talk.”",
    "He don't know where the keys is.",
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


def resolve(m):
    from huggingface_hub import model_info
    if not m.get("revision"):
        m["revision"] = model_info(m["repo"]).sha
    return m["revision"]


def clear_hf_cache():
    """The runner's disk is small: the originals go once a model is converted."""
    cache = os.path.join(os.path.expanduser("~"), ".cache", "huggingface")
    shutil.rmtree(cache, ignore_errors=True)


def tokenizer_check(m, d):
    """What the model's own tokenizer makes of a few texts and of a whole chat prompt: the add-on's tokenizer must
    give the same pieces (tests/logic_tests.cpp reads this file)."""
    from transformers import AutoTokenizer
    tok = AutoTokenizer.from_pretrained(m["repo"], revision=m["revision"])
    texts = list(SAMPLES) + PREVIEW
    msgs = [{"role": "system", "content": PREVIEW_SYSTEM}, {"role": "user", "content": PREVIEW[1]}]
    kwargs = {"enable_thinking": False} if m["chat"]["thinkTags"] else {}
    prompt = tok.apply_chat_template(msgs, tokenize=False, add_generation_prompt=True, **kwargs)
    texts.append(prompt)
    out = {"model": m["id"], "samples": []}
    for t in texts:
        out["samples"].append({"text": t, "tokens": tok.convert_ids_to_tokens(tok.encode(t, add_special_tokens=False))})
    with open(os.path.join(d, "tokenizer-check.json"), "w", encoding="utf-8") as f:
        json.dump(out, f, ensure_ascii=False, indent=1)
    return prompt


def preview(m, d):
    """A few results of the converted model, for the log (the review reads them)."""
    import ctranslate2
    if m["type"] == "chat":
        from transformers import AutoTokenizer
        tok = AutoTokenizer.from_pretrained(m["repo"], revision=m["revision"])
        gen = ctranslate2.Generator(d, device="cpu", compute_type="int8")
        kwargs = {"enable_thinking": False} if m["chat"]["thinkTags"] else {}
        for text in PREVIEW:
            msgs = [{"role": "system", "content": PREVIEW_SYSTEM}, {"role": "user", "content": text}]
            prompt = tok.apply_chat_template(msgs, tokenize=False, add_generation_prompt=True, **kwargs)
            tokens = tok.convert_ids_to_tokens(tok.encode(prompt, add_special_tokens=False))
            r = gen.generate_batch([tokens], max_length=128, include_prompt_in_result=False,
                                   end_token=["<|im_end|>", "<|endoftext|>"])[0]
            print(f"  {text!r}\n    -> {tok.convert_tokens_to_string(r.sequences[0])!r}")
    else:
        import sentencepiece as spm
        src = spm.SentencePieceProcessor(model_file=os.path.join(d, m["tokenizer"]["source"]))
        tgt = spm.SentencePieceProcessor(model_file=os.path.join(d, m["tokenizer"]["target"]))
        tr = ctranslate2.Translator(d, device="cpu", compute_type="int8")
        for text in ["Der Regen hatte seit drei Tagen nicht aufgehört.", "„Ich glaube nicht, dass er kommt“, sagte sie."]:
            pieces = [m["sourceToken"]] + src.encode(text, out_type=str) + ["</s>"]
            r = tr.translate_batch([pieces], beam_size=4)[0]
            print(f"  {text!r}\n    -> {tgt.decode(r.hypotheses[0])!r}")


def memory_mb(m, model_bin):
    mb = os.path.getsize(model_bin) / 1e6
    # int8 weights in memory, plus the work memory; chat models keep a cache of the text so far
    return int(mb * 1.2 + 700) if m["type"] == "chat" else int(mb * 2 + 100)


def main():
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    work = os.path.join(out, ".work")
    here = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    with open(os.path.join(here, "resources", "catalog.json"), encoding="utf-8") as f:
        old = json.load(f)
    catalog = {"format": 2, "engine": f"CTranslate2 {CT2_VERSION}", "models": []}
    for e in old["models"]:
        if e.get("release", old.get("release", "models-1")) == RELEASE:
            continue   # converted again below
        e.setdefault("release", old.get("release", "models-1"))
        if "kinds" not in e:
            e["kinds"] = [e.pop("kind")]
        e.setdefault("type", "seq2seq")
        catalog["models"].append(e)
    for m in MODELS:
        d = os.path.join(work, m["id"])
        shutil.rmtree(d, ignore_errors=True)
        rev = resolve(m)
        cmd = ["ct2-transformers-converter", "--model", m["repo"], "--revision", rev, "--output_dir", d,
               "--quantization", "int8", "--force", "--low_cpu_mem_usage"]
        if m["type"] == "chat":
            cmd += ["--copy_files", "tokenizer.json"]
        subprocess.run(cmd, check=True)
        tok_entry = {}
        if m["type"] == "chat":
            tokenizer_check(m, d)
            tok_entry = {"type": "bpe", "file": "tokenizer.json"}
        else:
            t = m["tokenizer"]
            for n in ("source", "target"):
                shutil.copy(hf_hub_download(m["repo"], t[n], revision=rev), os.path.join(d, t[n]))
            tok_entry = dict(t)
            vocab = json.load(open(os.path.join(d, "shared_vocabulary.json"), encoding="utf-8")) \
                if os.path.exists(os.path.join(d, "shared_vocabulary.json")) else \
                json.load(open(os.path.join(d, "source_vocabulary.json"), encoding="utf-8"))
            if m["sourceToken"] not in vocab:
                sys.exit(f"{m['id']}: {m['sourceToken']} is not in the vocabulary")
        print(f"{m['id']} ({m['repo']} at {rev}):", flush=True)
        preview(m, d)
        with open(os.path.join(d, "MODEL-LICENSE.txt"), "w", encoding="utf-8") as f:
            f.write(f"{m['title']}\n\n{m['credit']}\n\nLicense: {m['license']} ({m['licenseUrl']})\n"
                    f"Original: https://huggingface.co/{m['repo']} at revision {rev}\n")
        files = []
        for name in sorted(os.listdir(d)):
            p = os.path.join(d, name)
            entry = {"name": name, "asset": f"{m['id']}--{name}", "size": os.path.getsize(p), "sha256": sha256(p)}
            if entry["size"] > PART:
                entry["asset"] = ""
                entry["parts"] = []
                with open(p, "rb") as src:
                    i = 1
                    while True:
                        block = src.read(PART)
                        if not block:
                            break
                        asset = f"{m['id']}--{name}.part{i}"
                        with open(os.path.join(out, asset), "wb") as dst:
                            dst.write(block)
                        entry["parts"].append({"asset": asset, "size": len(block),
                                               "sha256": hashlib.sha256(block).hexdigest()})
                        i += 1
                os.remove(p)
            else:
                shutil.move(p, os.path.join(out, entry["asset"]))
            files.append(entry)
        e = {k: m[k] for k in ("id", "kinds", "type", "title", "summary", "license", "licenseUrl", "nonCommercial",
                               "credit")}
        e["release"] = RELEASE
        e["memoryMB"] = memory_mb(m, os.path.join(out, f"{m['id']}--model.bin")) \
            if os.path.exists(os.path.join(out, f"{m['id']}--model.bin")) else \
            int(sum(f["size"] for f in files if f["name"] == "model.bin") / 1e6 * (1.2 if m["type"] == "chat" else 2)
                + (700 if m["type"] == "chat" else 100))
        e["source"] = f"https://huggingface.co/{m['repo']}/tree/{rev}"
        e["tokenizer"] = tok_entry
        if m["type"] == "chat":
            e["chat"] = m["chat"]
        if m.get("sourceToken"):
            e["sourceToken"] = m["sourceToken"]
        e["files"] = files
        e["sizeMB"] = round(sum(f["size"] for f in files) / 1e6)
        catalog["models"].append(e)
        print(f"{m['id']}: {e['sizeMB']} MB, {len(files)} files, about {e['memoryMB']} MB of memory", flush=True)
        shutil.rmtree(d, ignore_errors=True)
        clear_hf_cache()
    shutil.rmtree(work, ignore_errors=True)
    with open(os.path.join(out, "catalog.json"), "w", encoding="utf-8") as f:
        json.dump(catalog, f, indent=2, ensure_ascii=False)
        f.write("\n")


if __name__ == "__main__":
    main()
