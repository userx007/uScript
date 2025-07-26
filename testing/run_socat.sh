#!/bin/bash

# Create a temporary file to capture socat output
tmpfile=$(mktemp)

# Start socat in the background and redirect its output to the temp file
socat -d -d pty,raw,echo=0 pty,raw,echo=0 2> "$tmpfile" &
socat_pid=$!

# Wait for socat to initialize and write PTY info
sleep 1

# Extract PTY device names
pty1=$(grep -oP 'PTY is (/dev/pts/[0-9]+)' "$tmpfile" | sed -n '1s/PTY is //p')
pty2=$(grep -oP 'PTY is (/dev/pts/[0-9]+)' "$tmpfile" | sed -n '2s/PTY is //p')

# Check if both PTYs were found
if [[ -z "$pty1" || -z "$pty2" ]]; then
    echo "Failed to extract PTY device names."
    kill $socat_pid
    exit 1
fi

echo "Using PTYs: $pty1 and $pty2"

sudo chmod a+rw "$pty1" "$pty2"


# Run the two executables with the PTYs as arguments
./uart_responder "$pty2" &
./uart_sender "$pty1"

# Wait for sender to finish, then clean up
wait

# Optionally kill socat if it's still running
kill $socat_pid 2>/dev/null
rm "$tmpfile"
