#!/bin/sh
# C89 host regression for the no-hardware P3 reference oracle.

set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
project_root=$(CDPATH= cd -- "$script_dir/.." && pwd)
work_dir=$(mktemp -d "${TMPDIR:-/tmp}/openstep-mga-reference.XXXXXX")
trap 'rm -rf "$work_dir"' EXIT HUP INT TERM

cc -std=c89 -pedantic -Wall -Wextra -Werror \
   -I"$project_root/reference" \
   "$project_root/reference/OpenStepMGAReference.c" \
   "$script_dir/openstep-mga-reference-test.c" \
   -o "$work_dir/openstep-mga-reference-test"
"$work_dir/openstep-mga-reference-test"
