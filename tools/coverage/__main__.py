"""Runs the AGDK coverage tool: `python3 tools/coverage <subcommand>`.

Python puts this directory on sys.path when it runs it, so the package
imports without installation.
"""

import sys

from agdk_coverage import cli

if __name__ == "__main__":
    sys.exit(cli.main())
