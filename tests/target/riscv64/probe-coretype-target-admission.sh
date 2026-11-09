#!/bin/sh
set -eu
: "${1:?pass the linked rv64 coretype target-admission test binary}"
"$1"
