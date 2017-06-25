#!/bin/bash

set -o errexit
set -o pipefail
set -o nounset
set -x

LD_LIBRARY_PATH=../src ./check_parser_utils
LD_LIBRARY_PATH=../src ./check_parser
LD_LIBRARY_PATH=../src ./check_rules
LD_LIBRARY_PATH=../src ./check_digest
LD_LIBRARY_PATH=../src ./check_mydomains


