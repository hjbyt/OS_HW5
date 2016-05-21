import argparse
import random


def main():
    args = parse_args()
    compile_random_pattern(args.n, args.output)


def parse_args():
    parser = argparse.ArgumentParser()
    parser.add_argument('n', type=int, help='dimension of output file (power of 2)')
    parser.add_argument('output', type=argparse.FileType('wb'), help='output file')
    args = parser.parse_args()
    if not is_power2(args.n):
        parser.error('n should be a power of 2')
    return args


def is_power2(num):
    return num != 0 and ((num & (num - 1)) == 0)


def compile_random_pattern(n, output):
    for _ in range(n*n):
        value = random.choice([b'\x00', b'\x01'])
        output.write(value)

if __name__ == '__main__':
    main()

