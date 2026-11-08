#!/usr/bin/env python3
import json, re, sys
from pathlib import Path
page=Path(sys.argv[1]).read_text()
header=Path(__file__).resolve().parent.parent/'payload/version.h'
version=re.search(r'^#define TURBO_VERSION "([^"]+)"',header.read_text(),re.M).group(1)
page=page.replace('{{TURBO_VERSION}}',version)
Path(sys.argv[2]).write_text('/* Generated from web/portal.html */\nstatic const char turbo_page[] =\n'+''.join(json.dumps(line,ensure_ascii=False)+'\n' for line in page.splitlines(keepends=True))+';\n')
