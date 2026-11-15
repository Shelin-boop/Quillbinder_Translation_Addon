# Translation and Proofreading (a Quillbinder add-on)

An add-on for [Quillbinder](https://github.com/Shelin-boop/Quillbinder_Public) (Linux, version 0.80.0 or later) that
checks English text for spelling and grammar and translates German text into English, on your own computer. Once a
model is downloaded, it works without any internet connection.

## What it does

- **Two places to work.** A tab of its own in the inspector (its icon is the first in the row: the text goes in at
  the top, the result comes out at the bottom, the controls are between them) and a window of its own (the text left,
  the result right, the controls above and between). Open the window with the add-on's button on the toolbar or with
  **Edit > Writing Tools > Translate and Proofread...**.
- **English text is proofread, German text is translated.** With **Automatic** the add-on tells the two apart by
  itself; you can also choose **Proofread English** or **Translate German to English**. **More Formal** lets the model
  also rewrite for a more formal tone.
- **Every change is marked.** Click a marked word: keep your original, or take the suggestion. Click any word of the
  result for other words: the rest of the sentence is fitted to the word you choose. In the text you put in
  (English), the right mouse button offers **Other Words for ...** as well.
- **Your rules.** No em-dashes (the models may not write one; a dash you wrote becomes a comma, which you can click to
  keep the dash), American spelling (colour to color, realise to realize, towards to toward, about 2,800 words),
  curly quotes and apostrophes, three periods instead of the ellipsis character, and your own replacements
  (`OK => okay`). All in **File > Options > Proofreading**.
- **From and into the editor.** **From Editor** takes the text selected in Quillbinder's editor and works on it;
  **Into Editor** puts the result in its place (one step to undo there); **Copy** copies it.
- **Safety checks.** When the model rewrites a sentence too much or doubles a word, your sentence is kept, with a note.

## Installing

1. Download `translation-proofreading-<version>.qbpkg` from the
   [releases](https://github.com/Shelin-boop/Quillbinder_Translation_Addon/releases) (the newest version), or from the
   pre-release **test** for the newest test build.
2. In Quillbinder: **File > Install Update or Add-on**, choose the file. Quillbinder says that the add-on is not signed
   by the Quillbinder project (only Quillbinder's own packages are); install it anyway. Restart Quillbinder.
3. **File > Options > Proofreading**: download a proofreading model and the translation model (see below). They are
   checked (size and checksum of every file) and then used without any connection.

## Models and licenses

The models are not part of the add-on. You choose which ones to download:

| Model | For | Download | Memory while working | License |
|---|---|---|---|---|
| CoEdIT Large (Grammarly) | proofreading: finds most errors | 788 MB | about 1.6 GB | CC BY-NC 4.0: **non-commercial use only** |
| CoEdIT Base (J. Bochi) | proofreading: smaller, misses many errors | 250 MB | about 0.6 GB | Apache-2.0 |
| OPUS-MT German to English (University of Helsinki) | translation | 79 MB | about 0.3 GB | Apache-2.0 |

Before CoEdIT Large is downloaded, the add-on asks you to accept its license. Whether a use counts as non-commercial is
your decision; this project gives no legal advice. The models are the originals from Hugging Face (fixed revisions),
converted for the engine with 8-bit weights; the converted files are on the pre-release
[models-1](https://github.com/Shelin-boop/Quillbinder_Translation_Addon/releases/tag/models-1), made by the workflow
"Models" (`models/convert.py`). On a computer without internet, **From Folder...** takes the files of that release from
a folder.

**Speed** (test machine with 2 processor cores): proofreading with CoEdIT Large about 40 words a second (5,000 words in
about 2 minutes); translating is much faster (the 74 words of the test text in under a second). Other words for a word
take one to three seconds. A computer with more cores is probably faster (not measured).

## Building (for developers)

Everything the add-on needs from Quillbinder is its public tool interface, `third_party/quillbinder/ToolApi.h`
(version 1.1, copied unchanged from the public source), and Qt. On Ubuntu 24.04:

    packaging/build-deps.sh        # oneDNN, CTranslate2 and SentencePiece into deps/install (once, about 40 minutes)
    cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH=/opt/qt/6.12.0/gcc_64 -DTP_TEST_SWITCHES=ON
    cmake --build build
    TP_MODELS_DIR=<folder with the files of models-1> QT_QPA_PLATFORM=offscreen ctest --test-dir build
    cmake -S . -B build-release -G Ninja -DCMAKE_PREFIX_PATH=/opt/qt/6.12.0/gcc_64
    cmake --build build-release && packaging/make-addon.sh build-release dist

A release is built against The Qt Company's Qt 6.12.0, the Qt inside Quillbinder's AppImage since 0.80.0 (the
workflow "Build" installs it with aqtinstall). The parts:

- `src/TranslateTool.*`: the writing tool (commands, inspector tab, toolbar button, options section).
- `src/Panel.*`: the panel in both layouts; `src/OptionsPage.*`: the options; `src/ModelStore.*`: catalog and download.
- `src/Logic.*`: language, sentences, rules, safety checks, the word comparison (Qt Core only).
- `src/engine/Engine.cpp`: the engine library (CTranslate2 with oneDNN, SentencePiece), loaded by the tool when it is
  first needed (`src/EngineApi.h` between the two).
- `tests/`: the checks: the text logic, the panel and options with the engine and the real models in a stand-in for
  Quillbinder (`FakeHost.h`), the plugin as Quillbinder loads it, and the libraries' visible symbols.
- `tools/make-us-spelling.py`: makes the American spelling list from SCOWL's Hunspell dictionaries.

The add-on's own code is under the Apache License 2.0 (LICENSE, NOTICE); the libraries inside it come under their own
licenses, listed in the package's `translation-proofreading/licenses/README.txt`.

Quillbinder is an independent program; this add-on is not made by or connected with Grammarly, the University of
Helsinki or the makers of the libraries it uses.
