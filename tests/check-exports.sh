#!/bin/bash
# The add-on's libraries may show only their entry points: a visible function of the same name as one of the program's
# would be mixed up with it (2026-11-15: qInitResources_resources).
set -euo pipefail
B=$1
fail=0
check() { # <library> <allowed pattern>
    local extra
    extra=$(nm -D --defined-only "$1" | awk '{print $3}' | grep -v -E "$2" || true)
    if [ -n "$extra" ]; then
        echo "FAIL: $(basename "$1") shows: $(echo "$extra" | head -5 | tr '\n' ' ')"
        fail=1
    else
        echo "ok: $(basename "$1")"
    fi
}
# template code of the C++ standard library (std::, marked weak or unique by the compiler) may stay: it is the same
# everywhere
STD='_ZN?St.*|_ZT[ISV]N?St.*|_ZZN?St.*'
check "$B/addon/plugins/libqb-translation-proofreading.so" "^(qt_plugin_instance|qt_plugin_query_metadata.*|$STD)\$"
check "$B/addon/translation-proofreading/libqb-tp-engine.so" "^(qb_tp_engine_create|$STD)\$"
exit $fail
