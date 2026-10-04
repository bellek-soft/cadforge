#!/usr/bin/env bash
# Reports the tail of log files as GitHub error annotations, so build failures can be
# read through the checks API (annotations) without downloading the full job log.
# Usage: annotate-tail.sh <title> <file>...
title="$1"; shift
for f in "$@"; do
  [ -f "$f" ] || continue
  body=$(tail -c 12000 "$f" | sed -e 's/%/%25/g' -e 's/\r/%0D/g' | awk 'BEGIN{ORS="%0A"} {print}')
  echo "::error title=${title} ($(basename "$f"))::${body}"
done
