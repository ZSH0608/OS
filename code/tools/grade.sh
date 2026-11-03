#!/bin/sh

set -u

make_cmd=make
if gmake --version >/dev/null 2>&1; then
    make_cmd=gmake
fi

# macOS 通常没有 GNU timeout；用 python 做限时运行
run_with_timeout() {
    seconds=$1
    shift
    python3 - "$seconds" "$@" <<'PY'
import subprocess, sys
seconds = float(sys.argv[1])
cmd = sys.argv[2:]
try:
    subprocess.run(cmd, timeout=seconds)
except subprocess.TimeoutExpired:
    pass
except FileNotFoundError as e:
    sys.stderr.write(str(e) + "\n")
    sys.exit(1)
PY
}

output=.qemu.out
score=0
run_case() {
    label=$1
    define=$2
    sentinel=$3
    points=$4
    seconds=$5
    shift 5
    output=".qemu.${define}.out"
    rm -f "$output"

    echo "Building $label..."
    if ! "$make_cmd" --quiet --no-print-directory clean; then
        echo "  - $label: FAIL (clean)"
        return
    fi
    if ! "$make_cmd" --quiet --no-print-directory "DEFS+=-D${define}"; then
        echo "  - $label: FAIL (build)"
        return
    fi

    echo "Running $label..."
    # ==================== 开始改代码 ====================
    # 本机新版 QEMU/OpenSBI：
    # 1) -device loader 不会设置 Domain0 Next Address（保持为 0），内核不会被跳转到；
    # 2) -kernel bin/kernel(ELF) 会因高位 VA 触发 DTB 放置失败；
    # 因此使用 -kernel bin/ucore.img（扁平二进制）+ 足够内存。
    # ==================== 改代码结束 ====================
    run_with_timeout "$seconds" qemu-system-riscv64 \
        -machine virt -nographic -m 128M -bios default \
        -kernel bin/ucore.img \
        -serial file:"$output" -monitor none -no-reboot >/dev/null 2>&1 || true

    passed=1
    if ! grep -F "$sentinel" "$output" >/dev/null 2>&1; then
        passed=0
    fi
    for pattern in "$@"; do
        if ! grep -F "$pattern" "$output" >/dev/null 2>&1; then
            passed=0
        fi
    done

    if [ "$passed" -eq 1 ]; then
        echo "  - $label: PASS ($points/$points)"
        score=$((score + points))
    else
        echo "  - $label: FAIL (0/$points)"
    fi
}

run_case "trap entry, register restore, and breakpoint EPC" \
    GRADE_LAB3_CONTEXT "[Test 1] Trap entry and breakpoint return: PASS" 35 8 \
    "Breakpoint at"
run_case "illegal instruction EPC advance and return" \
    GRADE_LAB3_ILLEGAL "[Test 2] Illegal instruction return: PASS" 25 8 \
    "Illegal instruction at" \
    "Supervisor software interrupt" \
    "Unknown interrupt: cause 15" \
    "cause    0x00000014"
run_case "re-armed supervisor timer and repeated trap return" \
    GRADE_LAB3_TIMER "[Test 3] Sustained supervisor timer: PASS" 40 30 \
    "100 ticks" \
    "ticks=200"

echo "Total Score: $score/100"
[ "$score" -eq 100 ]
