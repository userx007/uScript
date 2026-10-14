#!/bin/bash

find . -type f -name "uscript.ini" | while read -r file; do
    dir="$(dirname "$file")"
    new_file="$dir/config.ini"

    # Skip if config.ini already exists to avoid overwriting
    if [ ! -f "$new_file" ]; then
        mv "$file" "$new_file"
        echo "Renamed: $file -> $new_file"
    else
        echo "Skipped: $new_file already exists"
    fi
done

echo "Done."
