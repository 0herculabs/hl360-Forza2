#!/bin/sh
# Stand-in for `rexglue codegen` when assets/default.xex is absent: a clone of
# the repository builds the committed generated/ sources as they are. Put your
# default.xex in assets/ to regenerate them instead.
echo "forza2: assets/default.xex not found - using the committed generated/ code"
exit 0
