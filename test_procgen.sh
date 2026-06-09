#!/bin/bash
# Autonomous procedural map generator test script
# Skips the IWAD picker by passing -iwad directly

IWAD="/home/ericson-willians/.config/biaseddoom/doom2.wad"
BIN="/home/ericson-willians/workspace/BiasedDoom/build/biaseddoom"

if [ ! -f "$IWAD" ]; then
    echo "ERROR: IWAD not found at $IWAD"
    exit 1
fi

if [ ! -f "$BIN" ]; then
    echo "ERROR: binary not found at $BIN"
    exit 1
fi

run_test() {
    local seed=$1
    shift
    "$BIN" -nosound -nomusic -nogui -iwad "$IWAD" +dumpprocudmf "$seed" "$@" +quit 2>&1
}

case "${1:-test}" in
    seeds)
        shift
        if [ $# -eq 0 ]; then
            seeds=(1 42 99 123 999 12345 0 7 13 21)
        else
            seeds=("$@")
        fi
        for seed in "${seeds[@]}"; do
            echo "=== seed=$seed ==="
            run_test "$seed" | grep -E "Dead ends|Dumped|failed"
            locks=$(grep -c "locknumber" /tmp/procmap_test.udmf 2>/dev/null || echo 0)
            keys=$(grep -cE "type = [567];" /tmp/procmap_test.udmf 2>/dev/null || echo 0)
            sectors=$(grep -c "^sector$" /tmp/procmap_test.udmf 2>/dev/null || echo 0)
            things=$(grep -c "^thing$" /tmp/procmap_test.udmf 2>/dev/null || echo 0)
            echo "  sectors=$sectors things=$things locks=$locks keys=$keys"
        done
        ;;
    inspect)
        shift
        seed="${1:-12345}"
        run_test "$seed" | grep -E "Dead ends|Dumped|failed"
        echo "--- locknumber lines ---"
        grep -n "locknumber" /tmp/procmap_test.udmf
        echo "--- key thing lines ---"
        grep -nE "type = [567];" /tmp/procmap_test.udmf
        echo "--- exit trigger ---"
        grep -n "special = 243" /tmp/procmap_test.udmf
        ;;
    size)
        shift
        for size in 1 2 3 4 5; do
            echo "=== size=$size ==="
            run_test 12345 techbase 3 "$size" | grep -E "Dead ends|Dumped|failed"
            locks=$(grep -c "locknumber" /tmp/procmap_test.udmf 2>/dev/null || echo 0)
            keys=$(grep -cE "type = [567];" /tmp/procmap_test.udmf 2>/dev/null || echo 0)
            sectors=$(grep -c "^sector$" /tmp/procmap_test.udmf 2>/dev/null || echo 0)
            echo "  sectors=$sectors locks=$locks keys=$keys"
        done
        ;;
    udmf)
        head -100 /tmp/procmap_test.udmf
        ;;
    *)
        echo "Usage: $0 {seeds|inspect|size|udmf} [args...]"
        echo ""
        echo "  seeds [s1 s2 ...]   - Test multiple seeds and report stats"
        echo "  inspect [seed]      - Show lock/key lines in generated UDMF"
        echo "  size                - Test all size settings (1-5)"
        echo "  udmf                - Print first 100 lines of last UDMF"
        ;;
esac
