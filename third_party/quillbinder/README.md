# Quillbinder's tool interface

`ToolApi.h` is Quillbinder's interface for writing tools (version 1.1), copied unchanged from the public
Quillbinder source, https://github.com/Shelin-boop/Quillbinder_Public, file `src/tools/ToolApi.h` at commit
7b5166520f0d94f749acb7089c10a4e37161e620 (Quillbinder 0.91.1 beta, 2026-11-14). Licence: Apache License 2.0, as
Quillbinder (LICENSE in the repository's root).

The add-on needs nothing else of Quillbinder: everything goes through this header and Qt. The 1.0 and 1.1 parts
never change (the program reads tools built long ago), so the copy stays valid for later Quillbinder versions.
