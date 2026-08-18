#!/bin/sh
# C89 host regression for render-memory budget arithmetic.

set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
project_root=$(CDPATH= cd -- "$script_dir/.." && pwd)
work_dir=$(mktemp -d "${TMPDIR:-/tmp}/openstep-mga-render-budget.XXXXXX")
trap 'rm -rf "$work_dir"' EXIT HUP INT TERM

cc -std=c89 -pedantic -Wall -Wextra -Werror \
   -I"$project_root/profile" \
   "$project_root/profile/OpenStepMGARenderBudget.c" \
   "$script_dir/openstep-mga-render-budget-test.c" \
   -o "$work_dir/openstep-mga-render-budget-test"
"$work_dir/openstep-mga-render-budget-test"
echo "OPENSTEP_MGA_RENDER_BUDGET_HOST_TEST=pass"
