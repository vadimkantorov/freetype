# Unit and regression tests for the FreeType library

## Quick Start

### Download test fonts

Run the `tests/scripts/download-test-fonts.py` script, which will
download test fonts to the `tests/data/` directory first.

### Build the test programs

The tests are only built with the Meson build system, and
are disabled by default, enable the 'tests' option to compile
them, as in:

  meson setup out -Dtests=enabled
  meson compile -C out

### Run the test programs

  meson test -C out

The `truetype-cleartype-compatibility` test constructs fonts in memory and
needs no downloaded fonts.  It checks the legacy ClearType rules within
minimal v40, including native opt-out and preserved v35, monochrome, and
tricky-font behavior.
It exits with status 77 when interpreter version 40 is unavailable.

