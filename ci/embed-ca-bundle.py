#!/usr/bin/env python3
from pathlib import Path
import sys

if len(sys.argv) != 3:
    raise SystemExit("usage: embed-ca-bundle.py <input-pem> <output-header>")

pem = Path(sys.argv[1]).read_text()
Path(sys.argv[2]).write_text(f'''#ifndef D_EMBEDDED_CA_BUNDLE_H
#define D_EMBEDDED_CA_BUNDLE_H

namespace aria2 {{

static const char EMBEDDED_CA_BUNDLE[] = R"ARIA2CA({pem})ARIA2CA";
static const unsigned int EMBEDDED_CA_BUNDLE_LEN = sizeof(EMBEDDED_CA_BUNDLE) - 1;

}} // namespace aria2

#endif // D_EMBEDDED_CA_BUNDLE_H
''')
