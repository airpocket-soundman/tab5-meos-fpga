#!/usr/bin/env python3
"""Build the command reference page (docs/site/reference/index.html).

  python tools/build_reference.py

Shell commands are read from the command table in user/shell/shell.c, so the page cannot drift from the firmware;
the module that provides a command comes from the table below. The MicroPython API is docs/MICROPYTHON_API.md.
"""
import re
from html import escape
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / 'docs/site/reference'
GITHUB = 'https://github.com/airpocket-soundman/tab5-meos-fpga/blob/main/'

# command -> module that provides it (empty: core)
PROVIDED_BY = {'python': 'micropython', 'sl': 'sl', 'bt': 'bluetooth', 'uvc': 'uvc',
               'camera': 'camera', 'cam': 'camera', 'snap': 'camera'}

GROUPS = [
    ('Files and system', ['ls', 'cd', 'pwd', 'cat', 'echo', 'mkdir', 'rm', 'mount', 'files', 'edit', 'put', 'mem', 'uptime',
                          'ps', 'kill', 'history', 'scroll', 'help']),
    ('Devices and USB', ['lsdev', 'lsusb', 'usbreset', 'osk', 'keys', 'tap', 'gui', 'displaystatus', 'fptest']),
    ('Cameras and images', ['camera', 'cam', 'snap', 'uvc', 'image']),
    ('External I/O', ['io', 'gpio', 'adc', 'pwm', 'i2c', 'spi', 'uart']),
    ('Network', ['wifi', 'eth', 'ssh', 'sshd', 'tailscale', 'ts', 'web', 'browser', 'bt']),
    ('Programs', ['python', 'run', 'apps', 'install', 'stop', 'send', 'recv', 'sl', 'fpga']),
]

DETAIL_LINKS = {
    'io': 'docs/IO_API.md', 'gpio': 'docs/IO_API.md', 'adc': 'docs/IO_API.md', 'pwm': 'docs/IO_API.md',
    'i2c': 'docs/IO_API.md', 'spi': 'docs/IO_API.md', 'uart': 'docs/IO_API.md',
    'python': 'docs/MICROPYTHON.md', 'uvc': 'docs/USB_CAMERA_IMPLEMENTATION.md', 'camera': 'docs/CAMERA_IMPLEMENTATION.md',
    'snap': 'docs/CAMERA_IMPLEMENTATION.md', 'sshd': 'docs/SSH_SERVER_IMPLEMENTATION.md', 'lsusb': 'docs/USB_DIAGNOSTICS.md',
    'lsdev': 'docs/DEVICES.md', 'image': 'docs/IMAGE_VIEWER.md', 'fpga': 'docs/FPGA_LINK.md',
}


def shell_commands():
    text = (ROOT / 'user/shell/shell.c').read_text(encoding='utf-8', errors='replace')
    pattern = r'\{\s*"([a-z0-9_]+)"\s*,\s*\w+\s*,\s*"((?:[^"\\]|\\.)*)"\s*\}'
    return {name: help_.replace('\\"', '"') for name, help_ in re.findall(pattern, text)}


def inline(s):
    s = escape(s)
    s = re.sub(r'`([^`]+)`', r'<code>\1</code>', s)
    s = re.sub(r'\*\*([^*]+)\*\*', r'<strong>\1</strong>', s)
    return re.sub(r'\[([^\]]+)\]\(([^)]+)\)', lambda m: '<a href="%s">%s</a>' % (link(m[2]), m[1]), s)


def link(target):
    if target.startswith(('http://', 'https://', '#')):
        return target
    return GITHUB + 'docs/' + target if not target.startswith('docs/') else GITHUB + target


def split_row(line):
    """Split a Markdown table row on pipes that are not inside backticks."""
    cells, cur, tick = [], '', False
    for ch in line.strip().strip('|'):
        if ch == '`':
            tick = not tick
        if ch == '|' and not tick:
            cells.append(cur.strip())
            cur = ''
        else:
            cur += ch
    cells.append(cur.strip())
    return cells


def table(rows, heads):
    head = ''.join('<th scope="col">%s</th>' % inline(h) for h in heads)
    body = ''.join('<tr>' + ''.join('<td>%s</td>' % inline(c) for c in r) + '</tr>' for r in rows)
    return '<div class="table-wrap"><table><thead><tr>%s</tr></thead><tbody>%s</tbody></table></div>' % (head, body)


def markdown(text):
    out, rows = [], []
    in_code = False

    def flush():
        if rows:
            out.append(table(rows[1:], rows[0]))
            rows.clear()

    for line in text.splitlines():
        if line.startswith('```'):
            flush()
            out.append('</pre>' if in_code else '<pre>')
            in_code = not in_code
            continue
        if in_code:
            out.append(escape(line))
            continue
        if line.startswith('|'):
            cells = split_row(line)
            if not all(re.fullmatch(r'[-: ]+', c) for c in cells):
                rows.append(cells)
            continue
        flush()
        m = re.match(r'^(#{1,4}) (.*)', line)
        if m:
            level = min(len(m[1]) + 1, 5)
            anchor = re.sub(r'[^a-z0-9]+', '-', m[2].lower()).strip('-')
            out.append('<h%d id="%s">%s</h%d>' % (level, anchor, inline(m[2]), level))
        elif line.strip():
            out.append('<p>%s</p>' % inline(line))
    flush()
    return '\n'.join(out)


def shell_section(commands):
    seen = set()
    parts = []
    for title, names in GROUPS:
        rows = []
        for n in names:
            if n in commands:
                seen.add(n)
                rows.append(row(n, commands[n]))
        if rows:
            parts.append('<h3>%s</h3>%s' % (title, table(rows, ['Command and arguments', 'What it does', 'Module', 'Details'])))
    rest = [row(n, h) for n, h in commands.items() if n not in seen]
    if rest:
        parts.append('<h3>Other</h3>' + table(rest, ['Command and arguments', 'What it does', 'Module', 'Details']))
    return '\n'.join(parts)


def row(name, help_):
    # the help text usually starts with the usage, then a description separated by two or more spaces
    usage, _, desc = help_.partition('  ')
    if not usage.startswith(name):
        usage, desc = name, help_
    module = PROVIDED_BY.get(name, '')
    detail = DETAIL_LINKS.get(name)
    return ['`%s`' % usage.strip(), (desc.strip() or ' '), ('`%s`' % module) if module else 'core',
            '[docs](%s)' % detail if detail else '']


def main():
    commands = shell_commands()
    api = markdown((ROOT / 'docs/MICROPYTHON_API.md').read_text(encoding='utf-8'))
    doc = f'''<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1"><title>Tab5 MeOS - Command reference</title><meta name="description" content="Shell commands and the MicroPython API of MeOS on the M5Stack Tab5."><link rel="stylesheet" href="../style.css"></head>
<body><header><strong>Tab5 MeOS FPGA</strong><nav aria-label="Navigation"><a href="#shell">Shell</a><a href="#micropython">MicroPython</a><a href="../flash/">Firmware</a><a href="../">Roadmap</a><a href="https://github.com/airpocket-soundman/tab5-meos-fpga">GitHub ↗</a></nav></header>
<main><div class="hero"><h1>Command reference</h1><p class="lead">The shell commands and the MicroPython API. 日本語: 使い方の一覧です(英語)。The shell table is generated from the firmware sources; commands of a module that is not in your firmware size are absent from it (see <a href="../flash/">Firmware</a>).</p></div>
<section id="shell"><h2>Shell commands ({len(commands)})</h2><p>Type <code>help</code> for the same list on the device. Brackets mark optional arguments; the text is what the device prints.</p>{shell_section(commands)}</section>
<section id="micropython"><h2>MicroPython</h2>{api}</section></main>
<footer>Generated from <code>user/shell/shell.c</code> and <code>docs/MICROPYTHON_API.md</code> by <code>tools/build_reference.py</code>.</footer></body></html>
'''
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / 'index.html').write_text(doc, encoding='utf-8')
    print('Built the command reference (%d shell commands).' % len(commands))


if __name__ == '__main__':
    main()
