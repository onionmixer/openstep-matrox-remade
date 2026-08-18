#!/bin/sh
# Run two independently scheduled P2 contention clients and validate both logs.
# Usage: sh run-p2-lease-contend.sh /tmp/openstep-mga-lease-contend 1000

if [ "$#" -ne 2 ]; then
    echo "usage: $0 <lease-contend-binary> <successes>"
    exit 2
fi

client=$1
successes=$2
output_a=/tmp/openstep-mga-contend-a.out
output_b=/tmp/openstep-mga-contend-b.out

rm -f "$output_a" "$output_b"

"$client" "$successes" > "$output_a" 2>&1 &
pid_a=$!
"$client" "$successes" > "$output_b" 2>&1 &
pid_b=$!

wait "$pid_a"
child_a=$?
wait "$pid_b"
child_b=$?

grep "OPENSTEP_MGA_LEASE_CONTEND successes=$successes" "$output_a" | grep "result=0" > /dev/null
log_a=$?
grep "OPENSTEP_MGA_LEASE_CONTEND successes=$successes" "$output_b" | grep "result=0" > /dev/null
log_b=$?

cat "$output_a" "$output_b"
echo "OPENSTEP_MGA_P24_CONTEND_CHILD_STATUS=$child_a,$child_b"
echo "OPENSTEP_MGA_P24_CONTEND_LOG_STATUS=$log_a,$log_b"

if [ "$child_a" -ne 0 ] || [ "$child_b" -ne 0 ] || \
   [ "$log_a" -ne 0 ] || [ "$log_b" -ne 0 ]; then
    exit 1
fi
exit 0
