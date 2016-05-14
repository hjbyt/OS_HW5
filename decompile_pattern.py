import argparse
import math


def main():
    args = parse_args()
    decompile_pattern(args.input, args.output)


def parse_args():
    parser = argparse.ArgumentParser()
    parser.add_argument('input', type=argparse.FileType('rb'), help='input pattern file')
    parser.add_argument('output', type=argparse.FileType('w'), help='output .cells file')
    return parser.parse_args()


def decompile_pattern(input, output):
    content = input.read()
    if not is_power_of(len(content), 4):
        exit('Error, input file size is not a power of 4 (size = %d)' % len(content))
    n = int(math.sqrt(len(content)))
    i = 0
    for x in range(n):
        for y in range(n):
            value = ord(content[i])
            value = {0: '.', 1: 'O'}[value]
            i += 1
            output.write(value)
        output.write('\n')


def is_power_of(num, base):
    if base == 1 and num != 1: return False
    if base == 1 and num == 1: return True
    if base == 0 and num != 1: return False
    power = int(math.log(num, base) + 0.5)
    return base ** power == num


if __name__ == '__main__':
    main()
