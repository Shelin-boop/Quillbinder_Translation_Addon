# Translation and Proofreading (a Quillbinder add-on): notes for Claude Code

A writing-tool add-on for Quillbinder (Linux), built **without access to Quillbinder's private repository or project
documents**: everything comes from the public source (https://github.com/Shelin-boop/Quillbinder_Public) and this
repository. Keep it that way; that was the point of the experiment (2026-11-15).

## The person you work for

A writer, not a programmer, who wants results, not code. Explain in plain words what changed and what was checked.
If something is not verified, say "I don't know" first; mark guesses as guesses. Dates yyyy-mm-dd, 24 h times, SI
units. American English, Chicago style for texts meant to be read; "..." and not the single ellipsis character.
Longer texts for the user (reports) as an .odt file (A4). Ask when a request is unclear and expensive to redo.

## Facts the add-on depends on (from the public Quillbinder source)

- Tool interface 1.1: `third_party/quillbinder/ToolApi.h` (copied unchanged; see its README for the commit). Never
  change it; a new Quillbinder interface version would be copied in as a whole.
- Package format and install: Quillbinder's `docs/developer-guide.md`, sections 2, 3, 5, 6 (`packaging/make-package.py`
  writes it). Add-ons with Linux libraries carry `runtime` = `u2404-qt6.12-r1` and `minApp` >= `0.80.0`.
- Quillbinder's AppImage brings Qt 6.12.0 (Core, Gui, Widgets, Network ...) but no TLS plugin: the add-on brings
  `qtplugins/tls/libqopensslbackend.so` of the same Qt (the Speech Transcription add-on does the same).
- `ToolHost::contentDirs("translation-proofreading")` is the add-on's folder (engine, libgomp, icon, TLS plugin);
  `ToolHost::dataDir` keeps the models (`models/<id>/`, with `checked.txt` once every file's SHA-256 matched).
- A command's icon is a theme name only; the add-on's own icon goes on via `setCommandState` (1.1), which also makes
  the action checkable (the window command is "on" while the window is open).
- The plugin must not show symbols the program also has (2026-11-15: both had `qInitResources_resources`, and the
  add-on's catalog was not found in the real program): hidden visibility, `-Bsymbolic`, a resource file with a unique
  name, and `tests/check-exports.sh`.

## Build and checks

    packaging/build-deps.sh                     # once: oneDNN 3.9.1, CTranslate2 4.9.0, SentencePiece 0.2.1
    cmake -S . -B build -G Ninja -DTP_TEST_SWITCHES=ON [-DCMAKE_PREFIX_PATH=/opt/qt/6.12.0/gcc_64]
    cmake --build build
    TP_MODELS_DIR=<files of the release models-1> QT_QPA_PLATFORM=offscreen ctest --test-dir build --output-on-failure

Screenshots of the checks go to `build/screenshots` (`TP_SHOTS`): look at them after UI changes. The cloud session has
no Qt 6.12 (download.qt.io is not reachable); Ubuntu's Qt 6.4 is fine for development (a plugin built with an older Qt
loads in a newer one), but releases come only from the workflow "Build" (Qt 6.12.0 via aqtinstall).

## Releases

- `packaging/manifest.json` and `TP_VERSION` in CMakeLists.txt carry the version (make-addon.sh refuses a mismatch).
- A push to a work branch puts the package on the pre-release `test`; a push to `main` publishes `v<version>` once.
  Work on branches and merge to `main` when the writer agrees.
- Models: `models/convert.py` and the workflow "Models" make the release `models-1` and its `catalog.json`
  (copied into `resources/catalog.json`; the add-on checks downloads against it). A published model release is never
  changed: new or changed models get a new release name.

## Things learned (2026-11-15)

- Beam 2 let CoEdIT Large turn "bellow" into "downstairs"; the default is beam 4 ("Careful": 6). With oneDNN's
  numbers it still says "downstairs" there (Python's build with Intel MKL said "below"); the checks accept either.
- CTranslate2 goes wrong when it continues straight from an alternative (Marian models): other words are found in two
  steps (one decoding step for the candidates, then each sentence finished on its own).
- Curly quotes in the input made CoEdIT double a word; the models get plain quotes, and the rules put curly ones back.
- oneDNN's assembler stub has no stack note: link with `-z noexecstack`, else newer systems refuse the engine.
