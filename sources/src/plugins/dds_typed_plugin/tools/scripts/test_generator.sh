#!/bin/bash

# Exit immediately if a command exits with a non-zero status
set -e

# Check if the Python script exists in the current directory
PYTHON_SCRIPT="gen_publish_commands.py"
if [[ ! -f "$PYTHON_SCRIPT" ]]; then
    echo "Error: '$PYTHON_SCRIPT' not found in the current directory."
    exit 1
fi

# Check if python3 is available
if ! command -v python3 &> /dev/null; then
    echo "Error: python3 is not installed or not in PATH."
    exit 1
fi

echo "Scanning current directory for *_PSM.idl files..."

# Use find to locate all files matching the pattern
# -maxdepth 1 ensures we only look in the current folder (not subdirectories)
# -name "*_PSM.idl" matches the required pattern
# -print0 and read -d '' handle filenames with spaces/special chars safely
find . -maxdepth 1 -name "*_PSM.idl" -type f -print0 | while IFS= read -r -d '' file; do
    # Extract the prefix (everything before the last _PSM.idl)
    # We remove the ./ prefix first, then remove _PSM.idl from the end
    filename=$(basename "$file")
    prefix="${filename%_PSM.idl}"

    # Skip if prefix is empty (shouldn't happen with the pattern, but safe to check)
    if [[ -z "$prefix" ]]; then
        echo "Warning: Could not extract prefix from '$filename'. Skipping."
        continue
    fi

    echo "Processing: $filename -> Prefix: $prefix"

    # Command 1: Generate family-specific output
    echo "  Running: python3 $PYTHON_SCRIPT --family $prefix -o ${prefix}.txt"
    python3 "$PYTHON_SCRIPT" --family "$prefix" -o "${prefix}.txt"

    # Command 2: Generate script-format output
    echo "  Running: python3 $PYTHON_SCRIPT --family $prefix --format script -o script_${prefix}.txt"
    python3 "$PYTHON_SCRIPT" --family "$prefix" --format script -o "script_${prefix}.txt"

    echo "  Done."
done

echo "All files processed."
