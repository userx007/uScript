#!/usr/bin/env python3
import os
import sys
import re

# Configuration
TARGET_DIR = "."  # Change to your project directory
EXPECTED_LENGTHS = [13, 15]  # List of lengths you consider "correct". Any other length is a mismatch.
# If you only want to check for exactly one length, use [13] or [15].

def find_lt_hdr_matches(directory):
    """
    Recursively searches all files for #define LT_HDR.
    Returns a list of dictionaries with file info and string length.
    """
    matches = []

    # Regex to find #define LT_HDR followed by any whitespace and then a quoted string
    # We capture the content inside the quotes
    pattern = re.compile(r'#define\s+LT_HDR\s+"([^"]*)"')

    file_count = 0
    processed_files = 0

    print(f"Scanning directory: {os.path.abspath(directory)}")
    print(f"Looking for exact matches in files (ignoring binary/encoding errors)...")
    print("-" * 50)

    for root, dirs, files in os.walk(directory):
        # Optional: Skip hidden directories like .git, .vscode, build, etc.
        # Uncomment the next line if you want to exclude hidden folders
        # dirs[:] = [d for d in dirs if not d.startswith('.')]

        for filename in files:
            filepath = os.path.join(root, filename)
            processed_files += 1

            try:
                # Try to read as UTF-8, fallback to latin-1 or ignore errors
                try:
                    with open(filepath, 'r', encoding='utf-8', errors='ignore') as f:
                        lines = f.readlines()
                except Exception:
                    continue

                for line_num, line in enumerate(lines, 1):
                    # Strip newline characters for clean matching
                    clean_line = line.rstrip('\n\r')
                    match = pattern.search(clean_line)

                    if match:
                        string_value = match.group(1)
                        actual_len = len(string_value)

                        # Check if this length is in our "expected" list
                        is_correct_length = actual_len in EXPECTED_LENGTHS

                        matches.append({
                            'file': filepath,
                            'line_num': line_num,
                            'content': clean_line,
                            'string_value': string_value,
                            'length': actual_len,
                            'is_correct': is_correct_length
                        })

            except Exception as e:
                # Skip files that can't be read (e.g., binary files with strict encoding)
                continue

    return matches, processed_files

def main():
    if len(sys.argv) > 1:
        target = sys.argv[1]
    else:
        target = "."

    matches, total_files = find_lt_hdr_matches(target)

    print(f"\nProcessed {total_files} files.")
    print(f"Found {len(matches)} occurrences of #define LT_HDR.")
    print("-" * 50)

    if not matches:
        print("No #define LT_HDR found in any file.")
        return

    # Count by length
    length_counts = {}
    for m in matches:
        l = m['length']
        length_counts[l] = length_counts.get(l, 0) + 1

    print("\n--- SUMMARY BY STRING LENGTH ---")
    for length, count in sorted(length_counts.items()):
        status = "✅ CORRECT" if length in EXPECTED_LENGTHS else "❌ MISMATCH"
        print(f"Length {length}: {count} occurrences ({status})")

    print("-" * 50)
    print("--- DETAILED LIST OF ALL OCCURRENCES ---")

    mismatches = [m for m in matches if not m['is_correct']]
    corrects = [m for m in matches if m['is_correct']]

    if mismatches:
        print(f"\n🚨 Found {len(mismatches)} mismatches (Lengths not in {EXPECTED_LENGTHS}):")
        for m in mismatches:
            print(f"File: {m['file']}")
            print(f"Line {m['line_num']}: {m['content']}")
            print(f"String: \"{m['string_value']}\"")
            print(f"Length: {m['length']}")
            print("-" * 30)
    else:
        print("\n✅ All occurrences have correct lengths!")

    # If you want to see correct ones too, uncomment below:
    # if corrects:
    #     print(f"\n📝 Found {len(corrects)} correct occurrences:")
    #     for m in corrects:
    #         print(f"  {m['file']}:{m['line_num']} (Len: {m['length']})")

if __name__ == "__main__":
    main()
