#!/usr/bin/env python3
"""从 unified diff 里删掉匹配给定正则的「新增行」，并修正 hunk 行数。

用途：UniXNU 的官方补丁里有些对设备 archive 的调用只用
`#if DEVELOPMENT || DEBUG` 保护，没跟 `XNU_NATIVE_*` 开关挂钩；
不编那个 archive 时链接就会报 undefined symbol。删掉这几行（连同
正确的 hunk 计数）比改整份补丁安全。

用法：
    strip_patch_lines.py <patch> <regex> [<regex> ...]
"""

import io
import re
import sys


def main(argv):
    if len(argv) < 3:
        sys.stderr.write(__doc__)
        return 2

    path = argv[1]
    patterns = [re.compile(p) for p in argv[2:]]
    lines = io.open(path, encoding="utf-8").read().split("\n")

    out = []
    i = 0
    removed = 0
    while i < len(lines):
        line = lines[i]
        if not line.startswith("@@"):
            out.append(line)
            i += 1
            continue

        m = re.match(r"^@@ -(\d+),(\d+) \+(\d+),(\d+) @@(.*)$", line)
        if not m:
            out.append(line)
            i += 1
            continue

        header = line
        i += 1
        body = []
        while i < len(lines) and not lines[i].startswith("@@") and not lines[i].startswith("diff --git"):
            body.append(lines[i])
            i += 1

        kept = []
        for b in body:
            if b.startswith("+") and not b.startswith("+++"):
                text = b[1:]
                if any(p.search(text) for p in patterns):
                    removed += 1
                    continue
            kept.append(b)

        ctx = sum(1 for b in kept if b.startswith(" "))
        add = sum(1 for b in kept if b.startswith("+") and not b.startswith("+++"))
        rem = sum(1 for b in kept if b.startswith("-") and not b.startswith("---"))
        new_header = "@@ -%s,%d +%s,%d @@%s" % (m.group(1), ctx + rem, m.group(3), ctx + add, m.group(5))
        out.append(new_header)
        out.extend(kept)

    io.open(path, "w", encoding="utf-8", newline="\n").write("\n".join(out))
    print("removed %d added line(s) from %s" % (removed, path))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
