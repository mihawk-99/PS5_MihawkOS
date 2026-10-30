# SPDX-License-Identifier: GPL-3.0-or-later
"""Where a helper's start goes, step by step, from one run's lines (installers start a helper per custom action; each
cost about 1.7 s on the Pro in 2026-10-27's gates, against well under 0.1 s on a PC).

Every stamp is CLOCK_MONOTONIC in ms, the clock the title's spawn line, the helper's start lines and Wine's +timestamp
share:
  title  "wine-ps5: spawn native=P request_ms=R lock=.. read=.. answered=.. connected=.. attempts=N held=.. sent=.. ready=..
         warm=W" (each after R; -1: not reached or, warm=1, done before the request)  wine/ps5/pw_wine_process.c
  title  "wine-ps5: spawn served requester=R native=P received_ms=.. replied_ms=.." and
         "wine-ps5: child exit received native=P ms=.."                       the request's and the exit's way through
  parent "wine-ps5: spawn asked ms=.. replied_ms=.. child=P status=0"        the requesting helper's own log
  helper "wine-child: start at=<step> ms=M"                                   native/proton_wine_child.c start_stamp
         "wine-child: life summary ms=M server_calls=.. server_ms=.. ..."     at its end: server, file and section
         time, then where module loading went (convert_*, image_read_*, code_round_*, dl_found_*, dl_missed_*)
         "<s>.<ms>:<pid>:<tid>:trace:loaddll:..."                             Wine's first and last module lines
         "wine-child: payload ending monotonic_ms=M"
  helper "wine-ps5: fonts ready us=.. freetype_us=.. files_us=.. cache_us=.. cache_made=.."  font setup (patch 2153),
         "wine-ps5: managed wallpaper loaded desktop=WxH us=.."                  the wallpaper decode; both kept with
         the life summary's counters, as fonts_*_us and wallpaper_us
A helper's points become ms after its request; its steps are the gaps between them in time order, each named by where
it ends. A warm start's helper was made before its request (src/pw_warm_helper.h): its earlier points are negative and
not steps, and warm and cold starts have their own medians. summary() is the run record (tools/run-console.py writes it as "start_stages"); the medians are the verdict to
compare between runs:

  python3 tools/start_stages.py run <klog-dir> <run>     one run's logs, as run-console.py names them
  python3 tools/start_stages.py compare <a.json> <b.json>  two evidence records' medians side by side
"""
import json
import re
import statistics
import sys
from pathlib import Path

SPAWN = re.compile(r'wine-ps5: spawn native=(\d+) request_ms=(\d+) (.*)$')
FIELD = re.compile(r'(\w+)=(-?\d+)')
START = re.compile(r'^wine-child: start at=([\w-]+) ms=(-?\d+)$', re.M)
LOADDLL = re.compile(r'^(\d+)\.(\d{3}):[0-9a-f]+:[0-9a-f]+:trace:loaddll:', re.M)
LIFE = re.compile(r'^wine-child: life summary (.*)$', re.M)
ENDING = re.compile(r'^wine-child: payload ending monotonic_ms=(\d+)$', re.M)
SERVED = re.compile(r'wine-ps5: spawn served requester=(\d+) native=(\d+) received_ms=(\d+) replied_ms=(\d+)')
EXIT_RECEIVED = re.compile(r'wine-ps5: child exit received native=(\d+) ms=(\d+)')
ASKED = re.compile(r'wine-ps5: spawn asked ms=(\d+) replied_ms=(\d+) child=(\d+) status=0')
FONTS = re.compile(r'^wine-ps5: fonts ready (.*)$', re.M)
WALLPAPER = re.compile(r'^wine-ps5: managed wallpaper \w+ desktop=\d+x\d+ us=(\d+)$', re.M)
TITLE_POINTS = ('lock', 'read', 'answered', 'connected', 'held', 'sent', 'ready')


def helper(request_ms, title_fields, text, around=None):
    """One start: its points (ms after the request), its steps in time order, and its life summary's counters.
    around: other points of this start as absolute ms (the parent's ask and its answer, the title's receipt and reply,
    the title's receipt of this helper's exit)."""
    points = {name: title_fields[name] for name in TITLE_POINTS if title_fields.get(name, -1) >= 0}
    for name, ms in (around or {}).items():
        points[name] = ms - request_ms
    if text:
        for name, ms in START.findall(text):
            if int(ms) >= 0:
                points[name] = int(ms) - request_ms
        dlls = LOADDLL.findall(text)
        if dlls:
            points['first-dll'] = int(dlls[0][0]) * 1000 + int(dlls[0][1]) - request_ms
            points['last-dll'] = int(dlls[-1][0]) * 1000 + int(dlls[-1][1]) - request_ms
        if (ending := ENDING.search(text)):
            points['ending'] = int(ending.group(1)) - request_ms
    ordered = sorted(points.items(), key=lambda item: item[1])
    after = [(name, at) for name, at in ordered if at >= 0]
    steps = {name: at - before for (_, before), (name, at) in zip([('request', 0)] + after, after)}
    life = None
    if text and (found := LIFE.findall(text)):
        life = {key: int(value) for key, value in FIELD.findall(found[-1]) if key != 'ms'}
        exit_ms = life.pop('exit_ms', 0)   # the program's own exit, a point, not a counter
        if exit_ms > 0:
            points['exit'] = exit_ms - request_ms
            ordered = sorted(points.items(), key=lambda item: item[1])
            after = [(name, at) for name, at in ordered if at >= 0]
            steps = {name: at - before for (_, before), (name, at) in zip([('request', 0)] + after, after)}
    # Once-per-process setup user32 starts: font setup's steps and the wallpaper's decode (patch 2153).
    extra = {}
    if text and (found := FONTS.search(text)):
        extra = {'fonts_' + key.replace('_us', '') + '_us' if key != 'us' else 'fonts_us': int(value)
                 for key, value in FIELD.findall(found.group(1)) if key != 'cache_made'}
    if text and (found := WALLPAPER.search(text)):
        extra['wallpaper_us'] = int(found.group(1))
    if extra:
        life = {**(life or {}), **extra}
    return {'warm': bool(title_fields.get('warm')), 'attempts': title_fields.get('attempts'), 'points': dict(ordered),
            'steps': steps, 'life': life}


def summary(owner_lines, child_texts):
    """owner_lines: the title's klog lines; child_texts: {pid: helper log text}. None when no start was stamped."""
    starts, requests, around, requesters = {}, {}, {}, {}
    for line in owner_lines:
        if (m := SERVED.search(line)):
            native = int(m.group(2))
            requesters[native] = int(m.group(1))
            around.setdefault(native, {}).update({'received': int(m.group(3)), 'replied': int(m.group(4))})
        elif (m := EXIT_RECEIVED.search(line)):
            around.setdefault(int(m.group(1)), {})['exit-received'] = int(m.group(2))
    for text in child_texts.values():
        for asked, answered, child in ASKED.findall(text or ''):
            around.setdefault(int(child), {}).update({'asked': int(asked), 'answered-parent': int(answered)})
    for line in owner_lines:
        if (m := SPAWN.search(line)):
            fields = {key: int(value) for key, value in FIELD.findall(m.group(3))}
            native = int(m.group(1))
            requests[native] = int(m.group(2))
            starts[native] = helper(int(m.group(2)), fields, child_texts.get(native, ''), around.get(native))
    if not starts:
        return None
    record = {'starts': len(starts), 'warm_starts': sum(start['warm'] for start in starts.values())}
    for key, warm in (('median', False), ('median_warm', True)):
        chosen = [start for start in starts.values() if start['warm'] == warm]
        record[key] = {kind: {name: statistics.median(start[kind][name] for start in chosen if name in (start[kind] or {}))
                              for name in sorted({name for start in chosen for name in (start[kind] or {})})}
                       for kind in ('points', 'steps', 'life')} if chosen else None
    # The chain an installer's next action waits on: the same parent's previous child exited (its own stamp) and was gone
    # ('ending'), the parent woke and asked for the next one. Gaps in ms, absolute stamps compared.
    chain = []
    for native, start in starts.items():
        parent = requesters.get(native)
        asked = start['points'].get('asked')
        if parent is None or asked is None:
            continue
        asked += requests[native]
        earlier = [other for other in starts if requesters.get(other) == parent and requests[other] < requests[native]]
        if not earlier:
            continue
        previous = max(earlier, key=requests.get)
        points = starts[previous]['points']
        link = {'parent': parent, 'previous': previous, 'next': native}
        for name, key in (('exit', 'from_exit_ms'), ('exit-received', 'from_exit_received_ms'), ('ending', 'from_end_ms')):
            if name in points and points[name] + requests[previous] <= asked:
                link[key] = asked - (points[name] + requests[previous])
        chain.append(link)
    record['chain'] = chain
    keys = sorted({key for link in chain for key in link if key.endswith('_ms')})
    record['median_chain'] = {key: statistics.median(link[key] for link in chain if key in link) for key in keys}
    record['helpers'] = {str(pid): start for pid, start in sorted(starts.items())}
    return record


def from_run(klog_dir, run):
    folder = Path(klog_dir)
    owner = (folder / f'{run}.log').read_text(errors='replace').splitlines()
    children = {int(re.search(r'-(\d+)\.log$', path.name).group(1)): path.read_text(errors='replace')
                for path in folder.glob(f'{run}-wine-child-*.log')}
    return summary(owner, children)


def compare(a, b, key='median'):
    lines = []
    for kind in ('steps', 'points'):
        left, right = (a.get(key) or {}).get(kind, {}), (b.get(key) or {}).get(kind, {})
        for name in sorted(set(left) | set(right), key=lambda n: right.get(n, left.get(n, 0))):
            lines.append(f'{kind[:-1]:<6}{name:<16}{left.get(name, "-")!s:>10}{right.get(name, "-")!s:>10}')
    return '\n'.join(lines)


def main(argv):
    if len(argv) == 4 and argv[1] == 'run':
        record = from_run(argv[2], argv[3])
        print(json.dumps(record['median'] if record else None, indent=1))
        return 0 if record else 1
    if len(argv) == 4 and argv[1] == 'compare':
        records = [json.loads(Path(path).read_text()).get('start_stages') for path in argv[2:]]
        if not all(records):
            print('a record has no start_stages')
            return 1
        print('cold starts\n' + compare(*records) + '\nwarm starts\n' + compare(*records, key='median_warm'))
        return 0
    print(__doc__)
    return 2


if __name__ == '__main__':
    sys.exit(main(sys.argv))
