#!/bin/bash

ZC="./zc"
EXAMPLES_DIR="examples"
FAIL_COUNT=0
PASS_COUNT=0

echo "Running Example Transpilation Tests..."

while IFS= read -r file; do
    echo -n "Transpiling $file... "
    
    OUTPUT=$($ZC transpile "$file" 2>&1)
    EXIT_CODE=$?
    
    if [ $EXIT_CODE -eq 0 ]; then
        echo "PASS"
        PASS_COUNT=$((PASS_COUNT + 1))
        [ -f "out.c" ] && rm "out.c"
        [ -f "a.out" ] && rm "a.out"
    else
        echo "FAIL"
        echo "$OUTPUT"
        FAIL_COUNT=$((FAIL_COUNT + 1))
    fi

done < <(find "$EXAMPLES_DIR" -name "*.zc")

echo "----------------------------------------"
echo "Summary:"
echo "-> Passed: $PASS_COUNT"
echo "-> Failed: $FAIL_COUNT"
echo "----------------------------------------"

if [ $FAIL_COUNT -ne 0 ]; then
    exit 1
fi

exit 0
