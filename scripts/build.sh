#!/bin/sh
rm -rf .dist
bun build ./src/index.ts --outdir ./.dist --target=node --format=esm
