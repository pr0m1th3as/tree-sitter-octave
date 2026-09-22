#!/usr/bin/env python3
"""Run the dialect cases: each says which variants must accept it.

`tree-sitter test` cannot express "parses here, fails there", which is the
whole content of a dialect rule, so the variants would drift without this.

Case format in test/dialect/*.txt:

    === name of the case
    lenient: ok
    strict: reject
    matlab: ok
    ---
    <source>

A case names only the variants it is about.
"""

import os, subprocess, sys

ROOT = os.path.dirname (os.path.dirname (os.path.abspath (__file__)))
VARIANTS = {'lenient': ROOT,
            'strict': os.path.join (ROOT, 'strict'),
            'matlab': os.path.join (ROOT, 'matlab')}


def parse_cases (path):
    cases, name, want, lines, state = [], None, {}, [], 'head'
    for raw in open (path):
        line = raw.rstrip ('\n')
        if line.startswith ('==='):
            if name is not None:
                cases.append ((name, want, '\n'.join (lines) + '\n'))
            name, want, lines, state = line[3:].strip (), {}, [], 'head'
        elif state == 'head' and ':' in line and line.split (':')[0].strip () in VARIANTS:
            key, value = line.split (':', 1)
            want[key.strip ()] = value.strip ()
        elif line.startswith ('---'):
            state = 'body'
        elif state == 'body':
            lines.append (line)
    if name is not None:
        cases.append ((name, want, '\n'.join (lines) + '\n'))
    return cases


def accepts (source, variant, scratch):
    open (scratch, 'w').write (source)
    return subprocess.run (['tree-sitter', 'parse', '-q', scratch],
                           capture_output = True,
                           cwd = VARIANTS[variant]).returncode == 0


def main ():
    scratch = os.path.join (ROOT, '.dialect-case.m')
    failures, total = 0, 0
    try:
        for entry in sorted (os.listdir (os.path.join (ROOT, 'test', 'dialect'))):
            if not entry.endswith ('.txt'):
                continue
            path = os.path.join (ROOT, 'test', 'dialect', entry)
            for name, want, source in parse_cases (path):
                for variant, expected in want.items ():
                    total += 1
                    got = 'ok' if accepts (source, variant, scratch) else 'reject'
                    if got != expected:
                        failures += 1
                        print ('FAIL  %-44s %-8s expected %s, got %s'
                               % (name, variant, expected, got))
    finally:
        if os.path.exists (scratch):
            os.remove (scratch)
    print ('%d of %d dialect assertions pass' % (total - failures, total))
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit (main ())
