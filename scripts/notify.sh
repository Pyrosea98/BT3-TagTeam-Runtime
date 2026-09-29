#!/usr/bin/env bash
# [notify] Raise a notification in a running game, from a shell.
#
#   scripts/notify.sh <kind> <tone> <title> [body]
#
#     kind  net | ach
#     tone  none | info | good | great | bad
#
#   scripts/notify.sh ach great "I. Am. Powerful." "Reach Maximum Star Level"
#   scripts/notify.sh net bad  "Session ended" "The peer went away."
#
# Writes the request to a temp file and renames it into place, which is the only way the write is
# atomic -- the runtime removes the request as soon as it reads it, so a partial file would be read
# as a partial notification. The runtime polls once every ~15 frames.
set -euo pipefail

KIND="${1:?kind: net | ach}"
TONE="${2:?tone: none | info | good | great | bad}"
TITLE="${3:?title}"
BODY="${4:-}"

# The deploy, overridable for a second install or a build tree.
DEPLOY="${PS2X_DEPLOY:-$HOME/Escritorio/Dragon Ball Budokai Tenkaichi 3 Recompiled}"
DIR="$DEPLOY/savedata"

[[ -d "$DIR" ]] || { echo "no such directory: $DIR (set PS2X_DEPLOY)" >&2; exit 1; }

printf '%s %s %s :: %s\n' "$KIND" "$TONE" "$TITLE" "$BODY" > "$DIR/.notify.tmp"
mv "$DIR/.notify.tmp" "$DIR/notify.request"
echo "queued: $KIND/$TONE \"$TITLE\""
