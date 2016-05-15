
STEPS=${1:-1}
INPUT_MATRIX=${2:-glider8.bin}

gcc gol.c -o gol && ./gol $INPUT_MATRIX $STEPS
rm -f gol
