import argparse
import random
import sys

import validate


def fuzz(data):
    try:
        source = data.decode("utf-8")
    except UnicodeDecodeError:
        return
    try:
        validate.validate_documents([("test.toml", source)])
    except validate.ValidationError:
        return


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--smoke", type=int)
    args = parser.parse_args()
    if args.smoke is None:
        fuzz(sys.stdin.buffer.read())
    else:
        random_source = random.Random(53)
        seed = (validate.ROOT / "tools/settings/fixtures/valid/test.toml").read_bytes()
        for _ in range(args.smoke):
            data = bytearray(seed)
            for _ in range(random_source.randrange(1, 20)):
                data[random_source.randrange(len(data))] = random_source.randrange(256)
            fuzz(bytes(data))
        print(f"Fuzz smoke passed: {args.smoke} deterministic mutations")


if __name__ == "__main__":
    main()
