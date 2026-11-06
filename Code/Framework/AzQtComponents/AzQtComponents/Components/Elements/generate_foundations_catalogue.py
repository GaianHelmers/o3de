"""
Copyright (c) Contributors to the Open 3D Engine Project.
For complete copyright and license terms please see the LICENSE at the root of this distribution.

SPDX-License-Identifier: Apache-2.0 OR MIT

generate_foundations_catalogue.py -- writes Foundations.md, the catalogue of the flattened GUI's tokens, from Foundations.h.

    python generate_foundations_catalogue.py           write Foundations.md beside the header
    python generate_foundations_catalogue.py --check   exit 1 when Foundations.md is not what the header generates

Foundations.md is generated, never edited by hand: change Foundations.h, then run this script.
"""
import argparse
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
HEADER = os.path.join(HERE, "Foundations.h")
REPORT = os.path.join(HERE, "Foundations.md")

# ------------------------------------------------------------------------------
# The header's grammar
# ------------------------------------------------------------------------------

TOKEN_ID = re.compile(r"\$[a-z]+\.[A-Za-z0-9_+\-]+(?:\.[A-Za-z0-9_+\-]+)*")
DECLARATION = re.compile(r"^\s*inline constexpr (?P<type>const char\*|QColor|QMargins|int)\s+(?P<name>\w+)\s*"
                         r"(?:\((?P<args>[^)]*)\)|=\s*(?P<expr>[^;]+));\s*(?://\s*(?P<comment>.*))?$")
NAMESPACE_OPEN = re.compile(r"^\s*namespace (\w+)\s*$")
NAMESPACE_CLOSE = re.compile(r"^\s*\} // namespace (\w+)\s*$")
RULE = re.compile(r"^\s*// -{20,}\s*$")
DOC = re.compile(r"^\s*//! ?(.*)$")
FRAGMENT = re.compile(r"^Fragment of (\w+): (.*)$")
DERIVED_ENTRY = re.compile(r"^\s*//   (\$\S+) \| (.*)$")
DERIVED_MORE = re.compile(r"^\s*//     (\S.*)$")
COMMENT = re.compile(r"^\s*// ?(.*)$")
OUTER = ("AzQtComponents", "Foundations")


class Token:
    """One declaration of the header."""

    def __init__(self, name, kind, section):
        self.name = name                # qualified below Foundations: "Text::Primary", "Mid"
        self.kind = kind
        self.section = section
        self.value = ""
        self.reads = ""                 # the token it takes its value from, if any
        self.ids = []
        self.source = ""
        self.note = ""
        self.fragment_of = ""

    def root(self):
        """The root name the convention gives the token: $tier.<Name> for a tier, $<namespace>.<Name> below a namespace."""
        parts = self.name.split("::")
        return "$tier." + parts[0] if len(parts) == 1 else "$%s.%s" % (parts[-2].lower(), parts[-1])

    def purpose_ids(self):
        return [i for i in self.ids if i != self.root()]


class Derived:
    """One entry of the Derived section: id | from | formula | where it shows."""

    def __init__(self, group, text):
        self.group = group
        self.text = text


# ------------------------------------------------------------------------------
# Reading the header
# ------------------------------------------------------------------------------

def literal_value(kind, args):
    """The value a constructor writes: #RRGGBB[AA] for a colour, the four sides of margins."""
    numbers = [int(a.strip(), 0) for a in args.split(",") if a.strip()]
    if kind == "QColor":
        return "#" + "".join("%02X" % n for n in numbers)
    return ", ".join(str(n) for n in numbers)


def strip_ids(text):
    return " ".join(TOKEN_ID.sub("", text).split()).strip(" ,;")


def read_header(path):
    """(the doc of the Foundations namespace, sections in order, tokens by name, derived entries) of the header."""
    lines = open(path, encoding="utf-8").read().replace("\r\n", "\n").split("\n")
    intro, sections, tokens, derived = [], [], {}, []
    scope, doc, pending, group = [], [], [], ""
    index = 0
    while index < len(lines):
        line = lines[index]
        # A section heading: the text between two rules
        if RULE.match(line):
            heading = []
            index += 1
            while index < len(lines) and not RULE.match(lines[index]):
                heading.append(COMMENT.match(lines[index]).group(1))
                index += 1
            sections.append((" ".join(heading), []))
            doc, pending = [], []
            index += 1
            continue
        opened, closed = NAMESPACE_OPEN.match(line), NAMESPACE_CLOSE.match(line)
        declaration, doc_line = DECLARATION.match(line), DOC.match(line)
        entry, more = DERIVED_ENTRY.match(line), DERIVED_MORE.match(line)
        if opened and opened.group(1) == "Foundations":
            intro, doc = doc, []
        elif opened and opened.group(1) not in OUTER:
            scope.append(opened.group(1))
        elif closed and closed.group(1) not in OUTER:
            scope.pop()
        elif doc_line:
            doc.append(doc_line.group(1))
        elif declaration:
            token = read_declaration(declaration, scope, doc, sections[-1][0] if sections else "")
            tokens[token.name] = token
            sections[-1][1].append(token.name)
            doc = []
        elif entry:
            # The comment block right above a run of entries is their group's title
            if pending:
                group, pending = " ".join(pending), []
            derived.append(Derived(group, entry.group(1) + " | " + entry.group(2)))
        elif more and derived:
            derived[-1].text += " " + more.group(1)
        elif COMMENT.match(line):
            pending.append(COMMENT.match(line).group(1))
        else:
            doc, pending = [], []
        index += 1
    resolve_references(tokens)
    return intro, sections, tokens, derived


def read_declaration(match, scope, doc, section):
    token = Token("::".join(scope + [match.group("name")]), match.group("type"), section)
    if match.group("args") is not None:
        token.value = literal_value(token.kind, match.group("args"))
    else:
        expr = match.group("expr").strip()
        token.value = expr if expr.startswith('"') or re.match(r"^-?\d+$", expr) else ""
        token.reads = "" if token.value else expr
    comment = match.group("comment") or ""
    notes = []
    for text in doc:
        fragment = FRAGMENT.match(text)
        if fragment:
            token.fragment_of, purpose = fragment.group(1), fragment.group(2)
            notes.append(purpose)
        else:
            notes.append(text)
    token.ids = TOKEN_ID.findall(" ".join(doc) + " " + comment)
    token.source = strip_ids(comment) or strip_ids(" ".join(n for n in notes if ".qss" in n or "::" in n))
    token.note = strip_ids(" ".join(n for n in notes if not (".qss" in n and not token.fragment_of)))
    return token


def resolve_references(tokens):
    """A token written as another's name takes its value: the name is looked up in its own scope, then at the root."""
    for token in tokens.values():
        if not token.reads:
            continue
        name = token.reads.replace("Foundations::", "")
        scope = token.name.split("::")[:-1]
        candidates = ["::".join(scope + [name]), name]
        found = next((c for c in candidates if c in tokens), None)
        token.reads = found or token.reads
        seen = set()
        while found and found not in seen and tokens[found].reads and not tokens[found].value:
            seen.add(found)
            found = tokens[found].reads
        token.value = tokens[found].value if found in tokens else "(unresolved)"


# ------------------------------------------------------------------------------
# Writing the catalogue
# ------------------------------------------------------------------------------

def heading_block(heading):
    """A section of the header as a title (the words before its first colon) and the rest as a paragraph."""
    title, _, rest = heading.partition(": ")
    rest = rest[:1].upper() + rest[1:] + ("." if rest and not rest.endswith(".") else "")
    return ["## " + title, ""] + ([rest, ""] if rest else [])


def cell(text):
    return (text or "").replace("|", "\\|")


def render_doc(lines):
    """The header's doc as Markdown: paragraphs, and an indented list as bullets (a deeper line continues its bullet)."""
    out, paragraph = [], []
    lines = [line.replace("<", "&lt;").replace(">", "&gt;") for line in lines]
    for line in lines + [""]:
        indent = len(line) - len(line.lstrip())
        if not line.strip():
            if paragraph:
                out += [" ".join(paragraph), ""]
                paragraph = []
        elif indent == 2:
            if paragraph:
                out += [" ".join(paragraph), ""] if not paragraph[0].startswith("- ") else [" ".join(paragraph)]
            paragraph = ["- " + line.strip()]
        else:
            paragraph.append(line.strip())
    return out


def write_catalogue(intro, sections, tokens, derived):
    out = ["<!--",
           "Generated from Foundations.h by generate_foundations_catalogue.py. Do not edit: change the header, then run the script.",
           "-->",
           "",
           "# Foundations: the token catalogue of the flattened GUI",
           ""] + render_doc(intro) + [
           "The sections follow the header, from the primitive tiers down to the values derived from them.",
           ""]
    for heading, names in sections:
        if not names:
            continue
        out += heading_block(heading) + ["| Token | Root name | Value | Reads | Purpose ids | Source | Note |",
                                         "|---|---|---|---|---|---|---|"]
        for name in names:
            t = tokens[name]
            note = ("Fragment of %s: " % t.fragment_of if t.fragment_of else "") + t.note
            out.append("| `%s` | `%s` | %s | %s | %s | %s | %s |" % (name, t.root(), cell(t.value), "`%s`" % t.reads if t.reads else "",
                                                                    cell(" ".join(t.purpose_ids())), cell(t.source),
                                                                    cell(note.strip())))
        out.append("")
    fragments = [t for t in tokens.values() if t.fragment_of]
    if fragments:
        out += ["## Fragments", "",
                "A fragment equals a tier's value but serves another purpose, so it is a token of its own: a theme that changes the",
                "tier leaves it as it is.", "",
                "| Token | Value | Fragment of | Purpose |", "|---|---|---|---|"]
        for t in fragments:
            out.append("| `%s` | %s | `%s` | %s |" % (t.name, t.value, t.fragment_of, cell(t.note)))
        out.append("")
    derived_heading = next((h for h, n in sections if h.startswith("Derived")), "Derived")
    if derived:
        out += heading_block(derived_heading)
        group = None
        for entry in derived:
            if entry.group != group:
                group = entry.group
                out += ["", "### " + group.rstrip(":"), "", "| Id | From | Formula | Where it shows |", "|---|---|---|---|"]
            parts = [p.strip() for p in entry.text.split(" | ")]
            parts += [""] * (4 - len(parts))
            base = parts[1][len("from:"):].strip() if parts[1].startswith("from:") else parts[1]
            out.append("| `%s` | %s | %s | %s |" % (parts[0], cell(base), cell(parts[2]), cell(" | ".join(parts[3:]).strip(" |"))))
        out.append("")
    return "\n".join(out)


# ------------------------------------------------------------------------------
# Main
# ------------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[2] if __doc__ else "")
    parser.add_argument("--check", action="store_true", help="exit 1 when Foundations.md is not what the header generates")
    args = parser.parse_args()
    intro, sections, tokens, derived = read_header(HEADER)
    # The convention holds or nothing is written: every token carries its root name in the header
    missing = [t.name for t in tokens.values() if t.root() not in t.ids]
    if missing:
        print("tokens without their root name in Foundations.h: " + ", ".join(missing))
        return 1
    text = write_catalogue(intro, sections, tokens, derived)
    if args.check:
        current = open(REPORT, encoding="utf-8").read() if os.path.exists(REPORT) else ""
        if current != text:
            print("Foundations.md is not what Foundations.h generates: run generate_foundations_catalogue.py")
            return 1
        print("Foundations.md matches Foundations.h")
        return 0
    with open(REPORT, "w", encoding="utf-8", newline="\n") as handle:
        handle.write(text)
    print("wrote", REPORT)
    return 0


if __name__ == "__main__":
    sys.exit(main())
