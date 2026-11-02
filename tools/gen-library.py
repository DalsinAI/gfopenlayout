#!/usr/bin/env python3
"""gfopenlayout.library's version 10 calls, made from the C API itself.

Version 10 folds GFCSS's document calls, GFOpenLayout UI and its Amiga glue
into gfopenlayout.library. The calls are the C functions as the headers
declare them (gfopenlayout.h's CSS calls, gfopenlayout_ui.h and
gfopenlayout_ui_amiga.h), each under its library name (olui_set_text is
OLUI_SetText), with its arguments in registers: pointers in a0 to a3, then
the rest in d0 to d7.

From that one list this writes, between their "version 10" markers:
  library/gfopenlayout_lib.sfd        the calls, after version 7's
  include/inline/gfopenlayout.h       the program's inline calls
and whole:
  library/openlayout_lib_v10.h        the library's side: each call, and
                                      OL_V10_VECTORS for lib_vectors
  library/link/gfopenlayout_link.c    the C API through the library: a
                                      program that calls olui_new() and the
                                      rest links this instead of libolui.a
                                      (with gfopenlayout_link_base.c, which
                                      opens the library)

Run it after a header's calls change; --check says whether the files are
as it would write them (the Makefile's test runs that).
MIT, Copyright (c) 2026 AmiCradle Team."""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
FIRST_BIAS = 0x150          # after version 7's OL_FramePlace (0x14a)

# The C calls version 10 takes, in the order they go in the library: never
# reorder or take one out, only add at the end (a new version).
CORE = """ol_node_set_tag ol_node_tag ol_node_set_attr ol_node_attr
ol_node_set_css_state ol_node_css_state ol_node_is_generated
ol_node_first_child ol_node_next_sibling ol_document_add_stylesheet
ol_document_set_viewport ol_document_apply_styles ol_document_import_count
ol_document_import_url ol_document_add_import""".split()
UI = """olui_new olui_free olui_set_measure olui_add_css olui_load olui_on
olui_set_zoom olui_layout olui_draw olui_damaged olui_mouse olui_key olui_tick
olui_find olui_id olui_tag olui_get_text olui_set_text olui_get_value
olui_get_checked olui_set_checked olui_set_disabled olui_set_hidden
olui_set_progress olui_slider_value olui_set_slider olui_box olui_focus
olui_focused olui_get_scroll olui_set_scroll olui_select_index olui_select_set
olui_select_count olui_select_option_value olui_select_option_text
olui_select_add olui_select_clear olui_select_is_open olui_select_open
olui_number_value olui_number_decimals olui_set_number olui_get_selection
olui_set_selection olui_insert_text olui_edit olui_list_add olui_list_clear
olui_list_selected olui_list_select olui_item_open olui_item_is_open
olui_tabs_current olui_tabs_select olui_set_user olui_user
olui_theme_css olui_free_css""".split()
AMIGA = """olui_amiga_idcmp olui_amiga_new olui_amiga_free olui_amiga_theme olui_amiga_own
olui_amiga_images olui_amiga_fonts olui_amiga_layout olui_amiga_draw
olui_amiga_event""".split()
CALLS = [("core", n) for n in CORE] + [("ui", n) for n in UI] + [("amiga", n) for n in AMIGA]

# the program's side, where a call's plain one won't do: olui_theme_css's
# string is the program's to free(), so it is copied out of the library's
# memory into the program's own
LINK_BODY = {
    "olui_theme_css": """    char *lib = OLUI_ThemeCss(theme_text, dark, err, errlen), *mine;
    size_t n;
    if (!lib) return NULL;
    n = strlen(lib) + 1;
    mine = (char *)malloc(n);
    if (mine) memcpy(mine, lib, n);
    else if (err && errlen) strncpy(err, "out of memory", errlen - 1), err[errlen - 1] = 0;
    OLUI_FreeCss(lib);
    return mine;""",
    "olui_free_css": """    free(css);   /* olui_theme_css's copy, in the program's memory */""",
}

HEADERS = {"core": "include/gfopenlayout.h", "ui": "include/gfopenlayout_ui.h",
           "amiga": "include/gfopenlayout_ui_amiga.h"}


def lib_name(c):
    """ol_node_set_tag -> OL_NodeSetTag, olui_amiga_new -> OLUI_AmigaNew"""
    prefix, rest = ("OLUI_", c[5:]) if c.startswith("olui_") else ("OL_", c[3:])
    return prefix + "".join(w[:1].upper() + w[1:] for w in rest.split("_"))


def prototypes():
    found = {}
    for part, path in HEADERS.items():
        text = (ROOT / path).read_text()
        text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
        for m in re.finditer(r"^([A-Za-z_][\w \*]*?[\s\*])([a-z_][a-z0-9_]*)\s*\(([^;{]*?)\)\s*;", text, re.M):
            found[(part, m.group(2))] = (" ".join(m.group(1).split()).replace(" *", " *"), m.group(3))
    # every call the UI's headers declare is in the library
    listed = {n for _, n in CALLS}
    for part, name in sorted(found):
        if part != "core" and name.startswith("olui_") and name not in listed:
            sys.exit(f"gen-library: {HEADERS[part]} declares {name}, which is not in CALLS")
    calls = []
    for part, name in CALLS:
        if (part, name) not in found:
            sys.exit(f"gen-library: {name} is not in {HEADERS[part]}")
        ret, args = found[(part, name)]
        ret = ret.strip()
        params = []
        args = " ".join(args.split())
        if args and args != "void":
            for a in args.split(","):
                a = a.strip()
                m = re.match(r"(.*?[\s\*])([a-z_][a-z0-9_]*)$", a)
                if not m:
                    sys.exit(f"gen-library: {name}: an argument with no name: {a}")
                params.append((m.group(1).strip(), m.group(2)))
        calls.append((part, name, ret, params))
    return calls


def registers(name, params):
    a, d, regs = ["a0", "a1", "a2", "a3"], [f"d{i}" for i in range(8)], []
    for t, _ in params:
        pool = a if ("*" in t or t.endswith("_fn") or t == "olui_handler") and a else d
        if not pool:
            sys.exit(f"gen-library: {name} has more arguments than registers")
        regs.append(pool.pop(0))
    return regs


def is_void(ret):
    return ret == "void"


def generate():
    calls = prototypes()
    sfd, inline, lib, link, vectors = [], [], [], [], []
    bias = FIRST_BIAS
    part_seen = None
    for part, name, ret, params in calls:
        L = lib_name(name)
        regs = registers(name, params)
        if part != part_seen:
            title = {"core": "GFCSS in documents", "ui": "GFOpenLayout UI",
                     "amiga": "GFOpenLayout UI's Amiga glue"}[part]
            sfd.append(f"* version 10: {title}")
            inline.append(f"/* version 10: {title} */")
            lib.append(f"/* ---- {title} ---- */")
            part_seen = part
        argl = ", ".join(f"{t} {n}" if not t.endswith("*") else f"{t}{n}" for t, n in params)
        sfd.append(f"{ret} {L}({argl})({','.join(regs)})")
        names = ",".join(n for _, n in params)
        k = len(params)
        lp = f"LP{k}{'NR' if is_void(ret) else ''}"
        mac = [f"0x{bias:x}"]
        if not is_void(ret):
            mac.append(ret)
        mac.append(L)
        for (t, n), r in zip(params, regs):
            mac += [t, f"({n})", r]
        mac += ["", "OPENLAYOUT_BASE_NAME"]
        inline.append(f"#define {L}({names}) {lp}({', '.join(mac)})")
        # the library's side: the C call with its arguments from their registers
        largs = "".join(f"REG({r}, {t} {n}), " for (t, n), r in zip(params, regs))
        call = f"{name}({', '.join(n for _, n in params)})"
        body = f"{call};" if is_void(ret) else f"return {call};"
        lib.append(f"static {ret} {L}({largs}REG(a6, struct OpenLayoutLibBase *base))\n{{\n    (void)base;\n    {body}\n}}")
        vectors.append(f"(APTR){L}")
        # the program's side: the C call through the library
        cargs = ", ".join(f"{t} {n}" if not t.endswith("*") else f"{t}{n}" for t, n in params) or "void"
        cbody = f"{L}({', '.join(n for _, n in params)});"
        if name in LINK_BODY:
            link.append(f"{ret} {name}({cargs})\n{{\n{LINK_BODY[name]}\n}}")
        else:
            link.append(f"{ret} {name}({cargs})\n{{\n    {'' if is_void(ret) else 'return '}{cbody}\n}}")
        bias += 6
    v = ", \\\n    ".join(", ".join(vectors[i:i + 3]) for i in range(0, len(vectors), 3))
    libh = ("/* Made by tools/gen-library.py from the headers: don't edit. gfopenlayout.library\n"
            " * version 10's calls, each the C call with its arguments from their registers.\n"
            " * MIT, Copyright (c) 2026 AmiCradle Team. */\n"
            "#include <gfopenlayout_ui.h>\n#include <gfopenlayout_ui_amiga.h>\n\n"
            + "\n\n".join(lib)
            + "\n\n/* the last call's bias: lib_vectors is checked against it */\n"
            + f"#define OL_V10_LAST_BIAS 0x{bias - 6:x}\n"
            + "\n#define OL_V10_VECTORS \\\n    " + v + "\n")
    linkc = ("/* Made by tools/gen-library.py from the headers: don't edit. The C API of\n"
             " * GFOpenLayout's CSS calls, GFOpenLayout UI and its Amiga glue through\n"
             " * gfopenlayout.library 10: a program links this (libgfopenlayout_link.a) where it\n"
             " * linked libolui.a; gfopenlayout_link_base.c opens the library for it.\n"
             " * MIT, Copyright (c) 2026 AmiCradle Team. */\n"
             "#include <stdlib.h>\n#include <string.h>\n#include <proto/gfopenlayout.h>\n\n"
             + "\n\n".join(link) + "\n")
    return "\n".join(sfd), "\n".join(inline), libh, linkc, len(calls), bias - 6


def between(text, start, end, new):
    a = text.index(start)
    b = text.index(end, a)
    return text[:a] + start + "\n" + new + "\n" + text[b:]


def main():
    check = "--check" in sys.argv
    sfd_new, inline_new, libh, linkc, n, last = generate()
    out = {}
    p = ROOT / "library/gfopenlayout_lib.sfd"
    s = p.read_text()
    if "* ---- version 10 (tools/gen-library.py) ----" not in s:
        s = s.replace("==end", "* ---- version 10 (tools/gen-library.py) ----\n* ---- end of version 10 ----\n==end")
    out[p] = between(s, "* ---- version 10 (tools/gen-library.py) ----", "* ---- end of version 10 ----", sfd_new)
    p = ROOT / "include/inline/gfopenlayout.h"
    s = p.read_text()
    if "/* ---- version 10 (tools/gen-library.py) ---- */" not in s:
        s = s[:s.rindex("#endif")] + "/* ---- version 10 (tools/gen-library.py) ---- */\n/* ---- end of version 10 ---- */\n\n#endif\n"
    out[p] = between(s, "/* ---- version 10 (tools/gen-library.py) ---- */", "/* ---- end of version 10 ---- */", inline_new)
    out[ROOT / "library/openlayout_lib_v10.h"] = libh
    out[ROOT / "library/link/gfopenlayout_link.c"] = linkc
    stale = []
    for path, text in out.items():
        if not path.exists() or path.read_text() != text:
            stale.append(path)
            if not check:
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(text)
    if check:
        if stale:
            sys.exit("gen-library: out of date: " + ", ".join(str(p.relative_to(ROOT)) for p in stale))
        print(f"GEN-LIBRARY PASS {n} calls, last bias 0x{last:x}")
    else:
        print(f"gen-library: {n} calls (0x{FIRST_BIAS:x} to 0x{last:x}); wrote {len(stale)} files")


if __name__ == "__main__":
    main()
