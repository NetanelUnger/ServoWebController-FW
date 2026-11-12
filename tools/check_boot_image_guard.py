"""Compatibility entry point: OTA v2 tests now execute real C with host Flash faults.
The old --cc ARM compiler argument is accepted for existing invocations.
"""
import argparse
from check_ota import main

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', help='Legacy argument; the host test discovers MSVC')
    parser.parse_args()
    main()
