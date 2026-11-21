#!/usr/bin/env python3
# PyCPP regression test runner (cross-platform).
#
#   python tests/run_tests.py                       run every case
#   python tests/run_tests.py errors crashes        only these folders
#   python tests/run_tests.py -k unused             only cases whose name contains "unused"
#   python tests/run_tests.py --pycpp path/to/pycpp --compiler gcc -v
#
# Every .pycpp case starts with comment directives:
#   # expect-exit: 1            exit code of `pycpp file.pycpp` (a number or "nonzero"; default 0)
#   # expect-stderr: text       stderr must contain text           (repeatable)
#   # expect-stdout: text       stdout must contain text           (repeatable)
#   # expect-no-stderr: text    stderr must NOT contain text       (repeatable)
#   # expect-no-stdout: text    stdout must NOT contain text       (repeatable)
#   # args: --keep-temp         extra command line arguments
#   # timeout: 60               seconds before the case is killed
import argparse
import os
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
DIRECTIVE = re.compile(r'^#\s*(expect-exit|expect-stderr|expect-stdout|expect-no-stderr|expect-no-stdout|args|timeout):\s*(.*?)\s*$')


def find_pycpp(explicit):
    for candidate in (explicit, os.environ.get('PYCPP')):
        if candidate and Path(candidate).exists():
            return str(Path(candidate))
    found = shutil.which('pycpp')
    if found:
        return found
    root = HERE.parent.parent
    for name in ('pycpp.exe', 'pycpp'):
        for pattern in ('out/build/*/out/', 'out/build/*/debug_out/', 'build/out/', 'build/debug_out/', 'build/'):
            for folder in sorted(root.glob(pattern)):
                if (folder / name).is_file():
                    return str(folder / name)
    return None


def parse_case(path):
    exp = {'exit': 0, 'stderr': [], 'stdout': [], 'no-stderr': [], 'no-stdout': [], 'args': [], 'timeout': 60}
    for line in path.read_text(encoding='utf-8').splitlines()[:40]:
        m = DIRECTIVE.match(line)
        if not m:
            continue
        key, value = m.group(1).replace('expect-', ''), m.group(2)
        if key == 'exit':
            exp['exit'] = 'nonzero' if value == 'nonzero' else int(value)
        elif key == 'args':
            exp['args'] = value.split()
        elif key == 'timeout':
            exp['timeout'] = int(value)
        else:
            exp[key].append(value)
    return exp


def run_case(path, pycpp, compiler):
    exp = parse_case(path)
    cmd = [pycpp, path.name, '--compiler', compiler] + exp['args']
    env = dict(os.environ, NO_COLOR='1')
    temp = path.parent / 'temp'
    had_temp = temp.exists()
    start = time.time()
    try:
        proc = subprocess.run(cmd, cwd=str(path.parent), capture_output=True, text=True,
                              encoding='utf-8', errors='replace', timeout=exp['timeout'], env=env)
    except subprocess.TimeoutExpired:
        return False, ['timed out after %d s' % exp['timeout']], '', '', time.time() - start
    elapsed = time.time() - start
    problems = []
    code = proc.returncode
    if exp['exit'] == 'nonzero':
        if code == 0:
            problems.append('expected a non-zero exit code, got 0')
    elif code != exp['exit']:
        problems.append('expected exit code %s, got %s' % (exp['exit'], code))
    for text in exp['stderr']:
        if text not in proc.stderr:
            problems.append('stderr does not contain: ' + text)
    for text in exp['stdout']:
        if text not in proc.stdout:
            problems.append('stdout does not contain: ' + text)
    for text in exp['no-stderr']:
        if text in proc.stderr:
            problems.append('stderr must not contain: ' + text)
    for text in exp['no-stdout']:
        if text in proc.stdout:
            problems.append('stdout must not contain: ' + text)
    if temp.exists() and not had_temp:
        shutil.rmtree(temp, ignore_errors=True)
    return not problems, problems, proc.stdout, proc.stderr, elapsed


def main():
    parser = argparse.ArgumentParser(description='PyCPP regression tests')
    parser.add_argument('folders', nargs='*', help='case folders to run (default: all)')
    parser.add_argument('-k', dest='keyword', default='', help='only cases whose name contains this text')
    parser.add_argument('--pycpp', default=None, help='path to the pycpp executable')
    parser.add_argument('--compiler', default='gcc', help='C++ compiler used by pycpp (default: gcc)')
    parser.add_argument('-v', '--verbose', action='store_true', help='show stdout/stderr of every case')
    parser.add_argument('--list', action='store_true', help='list the cases and exit')
    opts = parser.parse_args()

    cases = []
    for folder in sorted(p for p in HERE.iterdir() if p.is_dir()):
        if opts.folders and folder.name not in opts.folders:
            continue
        for case in sorted(folder.glob('*.pycpp')):
            if opts.keyword in case.stem:
                cases.append(case)
    if opts.list:
        for case in cases:
            print('%s/%s' % (case.parent.name, case.stem))
        return 0
    if not cases:
        print('no test cases found')
        return 1

    pycpp = find_pycpp(opts.pycpp)
    if not pycpp:
        print('pycpp was not found. Build it, add it to PATH, or pass --pycpp <path> (or set PYCPP).')
        return 2
    print('using %s with --compiler %s\n' % (pycpp, opts.compiler))

    failed = []
    for case in cases:
        ok, problems, out, err, secs = run_case(case, pycpp, opts.compiler)
        name = '%s/%s' % (case.parent.name, case.stem)
        print('%s  %-42s %.1fs' % ('PASS' if ok else 'FAIL', name, secs))
        if not ok:
            failed.append(name)
            for problem in problems:
                print('      - ' + problem)
        if opts.verbose or not ok:
            for label, text in (('stdout', out), ('stderr', err)):
                text = text.strip()
                if text:
                    print('      %s:' % label)
                    for line in text.splitlines()[-25:]:
                        print('        | ' + line)
    print('\n%d passed, %d failed' % (len(cases) - len(failed), len(failed)))
    for name in failed:
        print('  failed: ' + name)
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
