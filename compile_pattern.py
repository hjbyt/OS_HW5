import argparse


def main():
    args = parse_args()
    compile_pattern(args.input, args.output, args.n)


def parse_args():
    parser = argparse.ArgumentParser()
    parser.add_argument('input', type=argparse.FileType('r'), help='input .cells file')
    parser.add_argument('output', type=argparse.FileType('wb'), help='output file')
    parser.add_argument('n', type=int, help='dimension of output file (power of 2)')
    args = parser.parse_args()
    if not is_power2(args.n):
        parser.error('n should be a power of 2')
    return args


def is_power2(num):
    return num != 0 and ((num & (num - 1)) == 0)


def compile_pattern(input, output, n):
    input_lines = input.read().splitlines()
    input_lines = [line for line in input_lines if not line.startswith('!') and line.strip() != '']
    for x in range(n):
        for y in range(n):
            try:
                value = input_lines[x][y]
                if value == '.':
                    value = 0
                elif value == 'O':
                    value = 1
                else:
                    raise ValueError('Unexpected char (%s)' % value)
            except IndexError:
                value = 0
            value = {0: b'\x00', 1: b'\x01'}[value]
            output.write(value)

if __name__ == '__main__':
    main()
