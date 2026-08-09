#!/usr/bin/env python3
"""BiasedDoom website generator (standard library only).

Reads content/*.html fragments, wraps them in template.html, generates
the API reference from ../docs/scripting/biaseddoom.pyi, and renders the
roadmap from ../docs/development/python-api-roadmap.md.

Usage:  python3 website/build.py
Output: website/out/
"""

import ast
import html
import os
import re
import sys
import time

HERE = os.path.dirname(os.path.realpath(__file__))
CONTENT = os.path.join(HERE, "content")
OUT = os.path.join(HERE, "out")
STUB = os.path.join(HERE, "..", "docs", "scripting", "biaseddoom.pyi")
ROADMAP = os.path.join(HERE, "..", "docs", "development", "python-api-roadmap.md")

PAGES = [
    ("index.html", "Home"),
    ("getting-started.html", "Getting Started"),
    ("api.html", "API Reference"),
    ("events.html", "Events"),
    ("actor-registry.html", "Actor Registry"),
    ("console-debugging.html", "Console & Debugging"),
    ("vscode.html", "VSCode"),
    ("heresy-integration.html", "Heresy Editor"),
    ("roadmap.html", "Roadmap"),
    ("downloads.html", "Downloads"),
]


def esc(text):
    return html.escape(text, quote=True)


# ---------------------------------------------------------------- template

def load_template():
    with open(os.path.join(HERE, "template.html"), encoding="utf-8") as f:
        return f.read()


def nav_html(current):
    items = []
    for filename, label in PAGES:
        if filename == current:
            items.append(f"<b>[{esc(label)}]</b>")
        else:
            items.append(f'<a href="{filename}">[{esc(label)}]</a>')
    return "\n        ".join(items)


def wrap(template, current, title, content):
    page = template
    page = page.replace("@@TITLE@@", esc(title))
    page = page.replace("@@NAV@@", nav_html(current))
    page = page.replace("@@CONTENT@@", content)
    stamp = time.strftime("%Y-%m-%d", time.gmtime())
    page = page.replace("@@STAMP@@", stamp)
    return page


# ------------------------------------------------- markdown subset (roadmap)

def md_inline(text):
    text = esc(text)
    text = re.sub(r"\*\*(.+?)\*\*", r"<b>\1</b>", text)
    text = re.sub(r"`([^`]+)`", r"<code>\1</code>", text)
    text = re.sub(r"\*(.+?)\*", r"<i>\1</i>", text)
    return text


def md_to_html(md):
    """Convert the small markdown subset used by the roadmap document."""
    out = []
    in_list = False
    in_table = False
    para = []

    def flush_para():
        nonlocal para
        if para:
            out.append("<p>" + md_inline(" ".join(para)) + "</p>")
            para = []

    def close_list():
        nonlocal in_list
        if in_list:
            out.append("</ul>")
            in_list = False

    def close_table():
        nonlocal in_table
        if in_table:
            out.append("</table>")
            in_table = False

    for raw in md.splitlines():
        line = raw.rstrip()

        if not line.strip():
            flush_para(); close_list(); close_table()
            continue

        if line.startswith("### "):
            flush_para(); close_list(); close_table()
            out.append(f"<h3>{md_inline(line[4:])}</h3>")
        elif line.startswith("## "):
            flush_para(); close_list(); close_table()
            out.append(f"<h2>{md_inline(line[3:])}</h2>")
        elif line.startswith("# "):
            flush_para(); close_list(); close_table()
            out.append(f"<h1>{md_inline(line[2:])}</h1>")
        elif line.startswith("---"):
            flush_para(); close_list(); close_table()
            out.append("<hr>")
        elif line.lstrip().startswith("- "):
            flush_para(); close_table()
            if not in_list:
                out.append("<ul>")
                in_list = True
            out.append("<li>" + md_inline(line.lstrip()[2:]) + "</li>")
        elif in_list and raw.startswith((" ", "\t")):
            # indented continuation of the current list item
            flush_para(); close_table()
            if out and out[-1].endswith("</li>"):
                out[-1] = out[-1][:-5] + " " + md_inline(line.strip()) + "</li>"
        elif line.startswith("|"):
            flush_para(); close_list()
            cells = [c.strip() for c in line.strip("|").split("|")]
            if all(set(c) <= set("-: ") for c in cells):
                continue  # separator row
            if not in_table:
                out.append('<table class="grid">')
                in_table = True
                tag = "th"
            else:
                tag = "td"
            out.append("<tr>" + "".join(f"<{tag}>{md_inline(c)}</{tag}>" for c in cells) + "</tr>")
        else:
            close_list(); close_table()
            para.append(line.strip())

    flush_para(); close_list(); close_table()
    return "\n".join(out)


def build_roadmap():
    with open(ROADMAP, encoding="utf-8") as f:
        return md_to_html(f.read())


# ----------------------------------------------------------- API reference

def signature_of(node):
    """Reconstruct a readable signature from a FunctionDef."""
    args = node.args
    parts = []
    pos = list(args.args)
    defaults = [None] * (len(pos) - len(args.defaults)) + list(args.defaults)
    for arg, default in zip(pos, defaults):
        text = arg.arg
        if arg.annotation is not None:
            text += ": " + ast.unparse(arg.annotation)
        if default is not None:
            text += " = " + ast.unparse(default)
        parts.append(text)
    if args.vararg is not None:
        parts.append("*" + args.vararg.arg)
    if args.kwonlyargs:
        if args.vararg is None:
            parts.append("*")
        for arg, default in zip(args.kwonlyargs, args.kw_defaults):
            text = arg.arg
            if arg.annotation is not None:
                text += ": " + ast.unparse(arg.annotation)
            if default is not None:
                text += " = " + ast.unparse(default)
            parts.append(text)
    if args.kwarg is not None:
        parts.append("**" + args.kwarg.arg)
    sig = "(" + ", ".join(parts) + ")"
    if node.returns is not None:
        sig += " -> " + ast.unparse(node.returns)
    return sig


def ann_type(node):
    if node.annotation is not None:
        return ast.unparse(node.annotation)
    return ""


def build_api():
    with open(STUB, encoding="utf-8") as f:
        source = f.read()
    tree = ast.parse(source)

    constants = []   # (name, type)
    functions = []   # (name, signature, docstring)
    classes = {}     # name -> {"props": [(n,t,doc)], "methods": [(n,sig,doc)], "doc": str}

    for node in tree.body:
        if isinstance(node, ast.AnnAssign) and isinstance(node.target, ast.Name):
            constants.append((node.target.id, ann_type(node)))
        elif isinstance(node, ast.Assign):
            for target in node.targets:
                if isinstance(target, ast.Name):
                    constants.append((target.id, ""))
        elif isinstance(node, ast.FunctionDef):
            functions.append((node.name, signature_of(node),
                              ast.get_docstring(node) or ""))
        elif isinstance(node, ast.ClassDef):
            entry = {"props": [], "methods": [], "doc": ast.get_docstring(node) or ""}
            for sub in node.body:
                if isinstance(sub, ast.AnnAssign) and isinstance(sub.target, ast.Name):
                    entry["props"].append((sub.target.id, ann_type(sub), ""))
                elif isinstance(sub, ast.FunctionDef):
                    entry["methods"].append((sub.name, signature_of(sub),
                                             ast.get_docstring(sub) or ""))
            classes[node.name] = entry

    # actor class constants live between the generated markers
    begin = source.index("@@GENERATED ACTOR CONSTANTS BEGIN@@")
    end = source.index("@@GENERATED ACTOR CONSTANTS END@@")
    actor_consts = []
    for line in source[begin:end].splitlines()[1:]:
        line = line.strip()
        m = re.match(r"([A-Z0-9_]+):\s*str\s*#?\s*(.*)", line)
        if m:
            actor_consts.append((m.group(1), m.group(2).strip()))

    out = []
    out.append("<h1>API Reference</h1>")
    out.append(f'<p><i>Generated from <code>docs/scripting/biaseddoom.pyi</code> — '
               f'{len(functions)} functions, {len(classes)} handle classes, '
               f'{len(actor_consts)} actor class constants. Import with '
               f'<code>import biaseddoom as bd</code>.</i></p>')
    out.append('<hr><p><b>On this page:</b> <a href="#modconst">Module constants</a> | '
               '<a href="#functions">Functions</a> | '
               + " | ".join(f'<a href="#cls-{n}">{n}</a>' for n in ("Actor", "Line", "Sector", "Player") if n in classes)
               + ' | <a href="#registry">Actor registry</a> | '
               '<a href="#actorconsts">Actor class constants</a></p>')

    out.append('<h2 id="modconst">Module constants</h2>')
    out.append('<table class="grid"><tr><th>Name</th><th>Type</th></tr>')
    for name, typ in constants:
        out.append(f"<tr><td><code>bd.{esc(name)}</code></td><td><code>{esc(typ)}</code></td></tr>")
    out.append("</table>")

    out.append('<h2 id="functions">Functions</h2>')
    for name, sig, doc in functions:
        out.append(f'<h3 id="fn-{esc(name)}"><code>bd.{esc(name)}{esc(sig)}</code></h3>')
        if doc:
            out.append("<p>" + md_inline(doc.replace("\n", " ")) + "</p>")

    for cname in ("Actor", "Line", "Sector", "Player"):
        if cname not in classes:
            continue
        entry = classes[cname]
        out.append(f'<h2 id="cls-{cname}">class bd.{cname}</h2>')
        if entry["doc"]:
            out.append("<p>" + md_inline(entry["doc"].replace("\n", " ")) + "</p>")
        if entry["props"]:
            out.append('<table class="grid"><tr><th>Property</th><th>Type</th></tr>')
            for pname, ptype, _ in entry["props"]:
                out.append(f"<tr><td><code>{esc(pname)}</code></td><td><code>{esc(ptype)}</code></td></tr>")
            out.append("</table>")
        for mname, msig, mdoc in entry["methods"]:
            if mname.startswith("__"):
                continue
            out.append(f'<h3><code>.{esc(mname)}{esc(msig)}</code></h3>')
            if mdoc:
                out.append("<p>" + md_inline(mdoc.replace("\n", " ")) + "</p>")

    if "_ActorsRegistry" in classes:
        entry = classes["_ActorsRegistry"]
        out.append('<h2 id="registry">bd.actors — the actor class registry</h2>')
        if entry["doc"]:
            out.append("<p>" + md_inline(entry["doc"].replace("\n", " ")) + "</p>")
        for mname, msig, mdoc in entry["methods"]:
            if mname.startswith("__"):
                continue
            out.append(f'<h3><code>bd.actors.{esc(mname)}{esc(msig)}</code></h3>')
            if mdoc:
                out.append("<p>" + md_inline(mdoc.replace("\n", " ")) + "</p>")

    out.append(f'<h2 id="actorconsts">Actor class constants ({len(actor_consts)})</h2>')
    out.append("<p>Attribute access on <code>bd.actors</code> maps each constant to its "
               "engine class name: <code>bd.actors.DOOM_IMP</code> → <code>\"DoomImp\"</code>. "
               "Mod- and script-defined classes resolve the same way at runtime.</p>")
    out.append('<table class="grid"><tr><th>Constant</th><th>Engine class</th></tr>')
    for const, cls in actor_consts:
        out.append(f"<tr><td><code>bd.actors.{esc(const)}</code></td><td><code>{esc(cls)}</code></td></tr>")
    out.append("</table>")

    return "\n".join(out)


# ------------------------------------------------------------------- build

def main():
    os.makedirs(OUT, exist_ok=True)
    template = load_template()

    # static assets
    import shutil
    shutil.copy(os.path.join(HERE, "style.css"), os.path.join(OUT, "style.css"))

    written = []
    for filename, label in PAGES:
        if filename == "api.html":
            content = build_api()
        elif filename == "roadmap.html":
            content = build_roadmap()
        else:
            with open(os.path.join(CONTENT, filename), encoding="utf-8") as f:
                content = f.read()
        page = wrap(template, filename, f"BiasedDoom — {label}", content)
        with open(os.path.join(OUT, filename), "w", encoding="utf-8") as f:
            f.write(page)
        written.append(filename)

    # every internal link must resolve
    known = set(PAGES[i][0] for i in range(len(PAGES)))
    broken = []
    for filename in written:
        with open(os.path.join(OUT, filename), encoding="utf-8") as f:
            for href in re.findall(r'href="([^"#]+)(?:#[^"]*)?"', f.read()):
                if href.endswith(".html") and href not in known:
                    broken.append(f"{filename} -> {href}")
    if broken:
        print("BROKEN LINKS:", file=sys.stderr)
        for b in broken:
            print("  " + b, file=sys.stderr)
        return 1

    print(f"built {len(written)} pages into {OUT}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
