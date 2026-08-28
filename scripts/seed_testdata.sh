#!/bin/sh
# Seeds sample files used to exercise QmlCommander by hand inside the test
# container. Each fixture targets one specific behaviour; see README.txt below.
set -eu

DATA=/root/testdata
mkdir -p "$DATA/nested/deep" "$DATA/it's a quoted folder" /root/scratch

# CRLF vs LF: editing and saving must not add a second CR to each line.
printf 'first\r\nsecond\r\nthird\r\n' > "$DATA/crlf-endings.txt"
printf 'first\nsecond\nthird\n'       > "$DATA/lf-endings.txt"

# Larger than the 256 KB preview limit: must open read-only.
head -c 400000 /dev/urandom | base64 > "$DATA/large-over-256kb.txt"

printf 'edit me\n'        > "$DATA/small-editable.txt"
printf 'nested payload\n' > "$DATA/nested/deep/payload.txt"

# Identical size and timestamp: exercises the equal-elements path in the sort
# comparator, which used to be an invalid ordering in descending mode.
for i in 1 2 3; do
    printf 'equal' > "$DATA/same-size-$i.txt"
done
touch -d '2020-01-01 00:00:00' \
    "$DATA/same-size-1.txt" "$DATA/same-size-2.txt" "$DATA/same-size-3.txt"

cat > "$DATA/README.txt" <<'EOF'
What each fixture is for
========================

large-over-256kb.txt
    Press F4. It must open READ-ONLY with a warning banner and no Save button.
    Only the first 256 KB is loaded, so saving would discard the rest.

crlf-endings.txt
    Press F4, change a character, Save, then reopen. Line endings must still be
    CRLF and must not gain an extra CR on each round trip. Compare against
    lf-endings.txt, which must stay LF.

it's a quoted folder
    Enter it and press Ctrl+T. A terminal must open in that folder. The single
    quote in the name must not be treated as shell syntax.

same-size-1/2/3.txt
    Identical size and identical timestamp. Sort by Size or by Date and toggle
    to descending. The order must stay stable and must not crash.

nested/
    Copy this folder into its own subfolder. This guard is NOT implemented yet
    (finding 5), so expect an ever-deepening tree - do not run it unattended.

/root/scratch
    Empty directory. Use it as the destination pane for copy and move tests.
EOF

echo "Seeded $DATA"
