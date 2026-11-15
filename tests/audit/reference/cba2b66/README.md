# Immutable performance reference

These nine source files are copied byte for byte from
`cba2b668f2309d426ebce65f2c69e572088780d3`, the already optimized baseline.
`manifest.json` records their SHA-256 hashes. They are comparison inputs and do
not enter the normal application build. Do not apply project changes here.

The audit tools use `reference_source.py` to read and verify the same content
from Git, or from these snapshots when a source ZIP has no repository history.
This permits independent old/new raster and upload comparisons after extraction.
