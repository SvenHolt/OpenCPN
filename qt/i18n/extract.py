#!/usr/bin/env python3
"""Collects the translatable strings of the Qt frontend.

Finds tr("…"), QObject::tr("…"), QCoreApplication::translate("ctx", "…")
(adjacent string literals are joined) in src/ and plugins/.

  extract.py                 print the number of strings
  extract.py --json out      write all source strings (sorted) as JSON
  extract.py --check de.json exit 1 if a string has no translation
"""
import json
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
LIT = r'"(?:[^"\\\n]|\\.)*"'
LITS = rf'((?:{LIT}\s*)+)'
PATTERNS = [
    re.compile(rf'\btr\(\s*{LITS}'),
    re.compile(rf'\btranslate\(\s*{LIT}\s*,\s*{LITS}'),
]


def unescape(lits):
    out = ''
    for m in re.finditer(LIT, lits):
        s = m.group(0)[1:-1]
        s = (s.replace('\\"', '"').replace('\\n', '\n').replace('\\t', '\t')
              .replace('\\\\', '\\'))
        out += s
    return out


def strings():
    found = set()
    for base in ('src', 'plugins'):
        for p in sorted((ROOT / base).rglob('*')):
            if p.suffix not in ('.cpp', '.h'):
                continue
            text = p.read_text(encoding='utf-8')
            for pat in PATTERNS:
                for m in pat.finditer(text):
                    s = unescape(m.group(1))
                    if s.strip():
                        found.add(s)
    return sorted(found)


def main():
    all_strings = strings()
    if len(sys.argv) >= 3 and sys.argv[1] == '--json':
        pathlib.Path(sys.argv[2]).write_text(
            json.dumps(all_strings, ensure_ascii=False, indent=1),
            encoding='utf-8')
    elif len(sys.argv) >= 3 and sys.argv[1] == '--check':
        tr = json.loads(pathlib.Path(sys.argv[2]).read_text(encoding='utf-8'))
        missing = [s for s in all_strings if s not in tr]
        for s in missing:
            print('missing:', repr(s))
        bad = [s for s in all_strings if s in tr and '%n' in s
               and not (isinstance(tr[s], list) and len(tr[s]) == 2)]
        for s in bad:
            print('needs [singular, plural]:', repr(s))
        # Placeholders (%1 … %9, %n) must survive the translation.
        ph = re.compile(r'%(?:L?[1-9]|n)')
        broken = []
        for s in all_strings:
            if s not in tr:
                continue
            forms = tr[s] if isinstance(tr[s], list) else [tr[s]]
            for f in forms:
                if sorted(ph.findall(f)) != sorted(ph.findall(s)):
                    broken.append(s)
        for s in broken:
            print('placeholders differ:', repr(s), '->', repr(tr[s]))
        print(f'{len(all_strings)} strings, {len(missing)} missing')
        sys.exit(1 if missing or bad or broken else 0)
    else:
        print(len(all_strings))


if __name__ == '__main__':
    main()
