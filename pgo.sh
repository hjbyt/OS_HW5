
STEPS=${1:-1}
INPUT_MATRIX=${2:-glider8.bin}
THREADS=${3:-1}

gcc pgol.c -o pgol -pthread && ./pgol $INPUT_MATRIX $STEPS $THREADS
rm -f pgol

