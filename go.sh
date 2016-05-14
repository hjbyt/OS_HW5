
STEPS=${1:-default}

gcc gol.c -o gol && ./gol glider8.bin $STEPS && python decompile_pattern.py result.bin -
rm -f gol result.bin

