#!/usr/bin/env python3
"""Add license and author info to C++ files under libs/drive.

For each .cpp file, the script looks for an initial /*...*/ comment block at
file start. If found, it ensures that the block contains our license section;
if not, the section is appended just before the closing */.  If there is no
initial block comment, a new one is prepended.

This mirrors the Python module script but uses C++ comment syntax.
"""

import os
import re

BASE_DIR = os.path.join(os.path.dirname(__file__), "..")
DRIVE_DIR = os.path.join(BASE_DIR, "libs", "drive")

LICENSE_TEXT = r"""
## 许可
GNU General Public License v3.0

## 作者
Kevin.Liu @ Make&Share
QQ: 47129927@qq.com
"""

# we'll insert this text inside C comment as shown:
SECTION = "\n" + LICENSE_TEXT + "\n"


def process_file(path: str):
    with open(path, "r", encoding="utf-8") as f:
        content = f.read()

    # look for initial C-style comment
    m = re.match(r"(\s*/\*[\s\S]*?\*/)([\s\S]*)", content)
    if m:
        header = m.group(1)
        rest = m.group(2)
        if "## 许可" in header:
            return False
        # insert section before closing */
        new_header = re.sub(r"\*/\s*$", SECTION + "*/", header)
        new_content = new_header + rest
    else:
        # no header, create one
        new_header = "/*" + SECTION + "*/\n\n"
        new_content = new_header + content

    with open(path, "w", encoding="utf-8") as f:
        f.write(new_content)
    return True


def main():
    updated = []
    for root, dirs, files in os.walk(DRIVE_DIR):
        for fn in files:
            if fn.endswith(".cpp") or fn.endswith(".h"):
                path = os.path.join(root, fn)
                if process_file(path):
                    updated.append(path)
    for p in updated:
        print("updated:", p)


if __name__ == "__main__":
    main()
