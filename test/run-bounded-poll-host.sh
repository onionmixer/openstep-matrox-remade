#!/bin/sh
# C89 host regression for bounded readiness/stability policy.

set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
project_root=$(CDPATH= cd -- "$script_dir/.." && pwd)
work_dir=$(mktemp -d "${TMPDIR:-/tmp}/openstep-mga-bounded-poll.XXXXXX")
trap 'rm -rf "$work_dir"' EXIT HUP INT TERM

cc -std=c89 -pedantic -Wall -Wextra -Werror \
   -I"$project_root/protocol" \
   "$project_root/protocol/OpenStepMGABoundedPoll.c" \
   "$script_dir/openstep-mga-bounded-poll-test.c" \
   -o "$work_dir/openstep-mga-bounded-poll-test"
"$work_dir/openstep-mga-bounded-poll-test"
echo "OPENSTEP_MGA_BOUNDED_POLL_HOST_TEST=pass"
