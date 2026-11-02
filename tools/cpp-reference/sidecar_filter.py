import re
import sys
from pathlib import Path

ENTRY = re.compile(
    r"[\\@](?:file|namespace|class|struct|union|fn|var|enum|typedef|mainpage|page)\b")


def to_comment_blocks(text):
    lines = text.splitlines() or [""]
    blocks = []
    for number, line in enumerate(lines):
        if number == 0:
            blocks.append("/** " + line)
        elif ENTRY.match(line):
            blocks.append("*/ /** " + line)
        else:
            blocks.append(" * " + line)
    blocks.append(" */")
    return "\n".join(blocks) + "\n"


def main():
    text = Path(sys.argv[1]).read_text(encoding="utf-8")
    sys.stdout.write(to_comment_blocks(text))
    return 0


if __name__ == "__main__":
    sys.exit(main())
