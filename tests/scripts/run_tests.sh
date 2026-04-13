#!/bin/bash

# Zen-C Test Suite Runner
# Usage: ./tests/scripts/run_tests.sh [options]
#
# Options:
#   --cpp                   Compile all tests in C++ mode
#   --cc <compiler>         Use a specific C compiler
#   --check                 Enable typechecking
#   -- file1.zc file2.zc    Any file listed after postfix `--` will be run
#                           If empty, scan for and run all tests
#
# Examples:
#   ./tests/scripts/run_tests.sh                                        # Test in C mode (default)
#   ./tests/scripts/run_tests.sh --cpp                                  # Test in C++ mode
#   ./tests/scripts/run_tests.sh --cc clang                             # Test with clang
#   ./tests/scripts/run_tests.sh --cc clang --cpp                       # Test in C++ mode with clang
#   ./tests/scripts/run_tests.sh -- std/test_hash.zc std/test_arena.zc  # Test only these files

# Configuration
ZC="./zc"
if [ ! -f "$ZC" ]; then
    if [ -f "./zc.exe" ]; then
        ZC="./zc.exe"
    elif [ -f "./build/zc" ]; then
        ZC="./build/zc"
    elif [ -f "./build/zc.exe" ]; then
        ZC="./build/zc.exe"
    fi
fi
TEST_DIR="tests"
PASSED=0
FAILED=0
SKIPPED=0
FAILED_TESTS=""

# Parse arguments
CC_NAME="gcc (default)"
USE_TYPECHECK=0
USE_CPP=0
TEST_FILES=()
sys_type=$(uname -s)
sys_arch=$(uname -m)
zc_args=()

collect_files=0
prev_arg=""
for arg in "$@"; do
    if [ "$arg" = "--" ]; then
        # After `--`, only .zc files to test are listed
        collect_files=1
        continue
    fi

    if [ $collect_files -eq 1 ]; then
        TEST_FILES+=("$arg")
        continue
    fi

    if [ "$prev_arg" = "--cc" ]; then
        CC_NAME="$arg"
    fi
    if [ "$arg" = "--check" ]; then
        USE_TYPECHECK=1
    fi
    if [ "$arg" = "--cpp" ]; then
        USE_CPP=1
    fi

    zc_args+=("$arg")
    prev_arg="$arg"
done

# Also check ZC_FLAGS for --cpp (backwards compat)
if [[ "$ZC_FLAGS" == *"--cpp"* ]]; then
    USE_CPP=1
    zc_args+=($ZC_FLAGS)
fi

# Build mode label
MODE="C"
if [ $USE_CPP -eq 1 ]; then
    MODE="C++"
fi

if [ ! -f "$ZC" ]; then
    echo "Error: zc binary not found. Please build it first."
    exit 1
fi

if [ ${#TEST_FILES[@]} -gt 0 ]; then
    TEST_LIST=$(printf "%s\n" "${TEST_FILES[@]}" | grep "$TEST_DIR"/)
else
    TEST_LIST=$(find "$TEST_DIR" -name "*.zc" -not -name "_*.zc" | sort)
fi

if [ ${#TEST_LIST[@]} -eq 0 ]; then
    echo "** Nothing to do **"
    exit 0
fi

echo "** Running Zen C test suite (mode: $MODE, compiler: $CC_NAME) **"

while read -r test_file; do
    [ -e "$test_file" ] || continue

    # Skip tests known to fail with TCC
    if [[ "$CC_NAME" == *"tcc"* ]]; then
        if [[ "$test_file" == *"test_intel.zc"* ]]; then
            echo "Skipping $test_file (Intel assembly not supported by TCC)"
            ((SKIPPED++))
            continue
        fi
        if [[ "$test_file" == *"test_attributes.zc"* ]]; then
            echo "Skipping $test_file (Constructor attribute not supported by TCC)"
            ((SKIPPED++))
            continue
        fi
        if [[ "$test_file" == *"test_simd_native.zc"* ]]; then
            echo "Skipping $test_file (SIMD vector extensions not supported by TCC)"
            ((SKIPPED++))
            continue
        fi
    fi

    # Skip tests known to fail with Zig
    if [[ "$CC_NAME" == *"zig"* ]]; then
        if [[ "$test_file" == *"plugins_suite.zc"* ]]; then
            echo "Skipping $test_file (Plugins not fully supported by zig cc yet)"
            ((SKIPPED++))
            continue
        fi
    fi

    # Skip C++-incompatible tests
    if [ $USE_CPP -eq 1 ]; then
        # Inline assembly tests use C-only syntax
        if [[ "$test_file" == *"test_asm.zc"* ]] || \
           [[ "$test_file" == *"test_asm_clobber.zc"* ]] || \
           [[ "$test_file" == *"test_asm_arm64.zc"* ]] || \
           [[ "$test_file" == *"test_asm_clobber_arm64.zc"* ]] || \
           [[ "$test_file" == *"test_intel.zc"* ]]; then
            echo "Skipping $test_file (inline assembly not tested in C++ mode)"
            ((SKIPPED++))
            continue
        fi
    fi

    # Skip architecture-specific tests
    if [[ "$sys_arch" != *"86"* && "$sys_arch" != "amd64" ]]; then
        if [[ "$test_file" == *"test_asm.zc"* ]] || \
           [[ "$test_file" == *"test_asm_clobber.zc"* ]] || \
           [[ "$test_file" == *"test_intel.zc"* ]]; then
            echo "Skipping $test_file (x86 assembly not supported on $sys_arch)"
            ((SKIPPED++))
            continue
        fi
    fi

    if [[ "$sys_arch" != *"arm64"* && "$sys_arch" != "aarch64" ]]; then
        if [[ "$test_file" == *"_arm64.zc"* ]]; then
            echo "Skipping $test_file (ARM64 assembly not supported on $sys_arch)"
            ((SKIPPED++))
            continue
        fi
    fi

    # Skip tests that require typechecking if not enabled
    if grep -q "// REQUIRE: CHECK" "$test_file"; then
        if [ $USE_TYPECHECK -eq 0 ]; then
             echo "Skipping $test_file (requires --check)"
             ((SKIPPED++))
             continue
        fi
    fi

    echo -n "Testing $test_file... "
    
    # Add -w to suppress warnings as requested
    tmp_out="test_out_$$.out"
    cmd_str="$ZC run \"$test_file\" -o \"$tmp_out\" -w --emit-c \"${zc_args[@]}\""
    output=$(set -o pipefail; $ZC run "$test_file" -o "$tmp_out" -w --emit-c "${zc_args[@]}" 2>&1 | tr -d '\0')
    exit_code=$?
    
    # Check for expected failure annotation
    if grep -q "// EXPECT: FAIL" "$test_file"; then
        if [ $exit_code -ne 0 ]; then
            echo "PASS (Expected Failure)"
            ((PASSED++))
        else
            echo "FAIL (Unexpected Success)"
            ((FAILED++))
            FAILED_TESTS="$FAILED_TESTS\n- $test_file (Unexpected Success)"
        fi
    else
        if [ $exit_code -eq 0 ]; then
            echo "PASS"
            ((PASSED++))
        else
            echo "FAIL"
            echo "----------------------------------------"
            echo "Exit Code: $exit_code"
            echo "Command: $cmd_str"
            echo "Output:"
            echo "$output"
            # Keep the output file for debugging
            if [ -f "$tmp_out" ]; then
                echo "Program output preserved at: $tmp_out"
            fi
            if [ -f "$tmp_out.c" ]; then
                echo "Generated C source preserved at: $tmp_out.c"
                echo "--- Source Begin ---"
                cat "$tmp_out.c"
                echo "--- Source End ---"
            elif [ -f "$tmp_out.cpp" ]; then
                echo "Generated C++ source preserved at: $tmp_out.cpp"
                echo "--- Source Begin ---"
                cat "$tmp_out.cpp"
                echo "--- Source End ---"
            fi
            echo "----------------------------------------"
            ((FAILED++))
            FAILED_TESTS="$FAILED_TESTS\n- $test_file"
        fi
    fi
    
    # If passed, clean up. If failed, let the user see the file.
    if [ $exit_code -eq 0 ]; then
        rm -f "$tmp_out" "${tmp_out}.cpp"
    fi
done <<< "$TEST_LIST"

echo "----------------------------------------"
echo "Results ($MODE mode):"
echo "-> Passed:  $PASSED"
echo "-> Failed:  $FAILED"
echo "-> Skipped: $SKIPPED"
echo "----------------------------------------"

if [ $FAILED -ne 0 ]; then
    echo -e "Failed tests:$FAILED_TESTS"
    rm -f test_out_*.out out.c out.cpp
    exit 1
else
    echo "All tests passed!"
    rm -f test_out_*.out out.c out.cpp
    exit 0
fi
