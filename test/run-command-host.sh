#!/bin/sh
# C89 host regression for P3 command-envelope validation.

set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
project_root=$(CDPATH= cd -- "$script_dir/.." && pwd)
work_dir=$(mktemp -d "${TMPDIR:-/tmp}/openstep-mga-command.XXXXXX")
trap 'rm -rf "$work_dir"' EXIT HUP INT TERM

cc -std=c89 -pedantic -Wall -Wextra -Werror \
   -I"$project_root/protocol" \
   "$project_root/protocol/OpenStepMGACommand.c" \
   "$script_dir/openstep-mga-command-test.c" \
   -o "$work_dir/openstep-mga-command-test"
"$work_dir/openstep-mga-command-test"
