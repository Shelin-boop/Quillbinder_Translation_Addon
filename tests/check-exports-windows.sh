#!/bin/bash
# Windows (2026-11-16): the add-on's DLLs may export only their entry points, as tests/check-exports.sh checks on Linux
# (dumpbin of MSVC; runs in Git Bash inside MSVC's environment).
set -uo pipefail
B=$1
fail=0
check() { # <dll> <allowed pattern>
    local names extra
    names=$(dumpbin -nologo -exports "$1" | tr -d '\r' | awk '/ordinal +hint +RVA +name/{f=1; next} f && NF >= 4 {print $4}')
    [ -n "$names" ] || { echo "FAIL: $(basename "$1") exports nothing (or dumpbin failed)"; fail=1; return; }
    extra=$(echo "$names" | grep -v -E "$2" || true)
    if [ -n "$extra" ]; then
        echo "FAIL: $(basename "$1") exports: $(echo "$extra" | head -5 | tr '\n' ' ')"
        fail=1
    else
        echo "ok: $(basename "$1") ($(echo "$names" | tr '\n' ' '))"
    fi
}
check "$B/addon/plugins/qb-translation-proofreading.dll" '^(qt_plugin_instance|qt_plugin_query_metadata.*)$'
check "$B/addon/translation-proofreading/qb-tp-engine.dll" '^qb_tp_engine_create$'
exit $fail
