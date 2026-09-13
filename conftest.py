"""Root conftest.py — adds the repo root to sys.path so `import daemon.*` resolves.

daemon/ goes on the path too: the macOS daemon runs as a script and imports its
sibling modules (e.g. `usage_extras`) by bare name.
"""

import os
import sys

sys.path.insert(0, os.path.dirname(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "daemon"))
