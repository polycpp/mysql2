#!/usr/bin/env python3
from __future__ import annotations

import subprocess
from pathlib import Path


def run(cmd: list[str], cwd: Path) -> None:
    print(f"==> {' '.join(cmd)}")
    subprocess.run(cmd, cwd=cwd, check=True)


def main() -> None:
    docs_dir = Path(__file__).resolve().parent
    (docs_dir / "build/doxygen/xml").mkdir(parents=True, exist_ok=True)
    run(["doxygen", "Doxyfile"], docs_dir)
    run(["sphinx-build", "-b", "html", "-W", "--keep-going", "sphinx", "build/html"], docs_dir)
    print()
    print("Open docs/build/html/index.html in a browser.")


if __name__ == "__main__":
    main()
