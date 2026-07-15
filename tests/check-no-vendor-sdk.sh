#!/bin/sh
set -eu

root=${1:-.}

if find "$root" \( -path "$root/.git" -o -path "$root/build" -o -path "$root/build-*" \) -prune -o -type f \( \
  -iname 'libdvrnetsdk.so*' -o \
  -iname 'libnetclientsdk.so*' -o \
  -iname 'dvr_net_sdk.h' -o \
  -iname '*.dll' -o \
  -iname '*.dylib' -o \
  -iname '*.so' -o \
  -iname '*.so.*' \
\) -print | grep -q .; then
  echo "Vendor SDK or native binary artifact found in repository" >&2
  exit 1
fi

if find "$root" \( -path "$root/.git" -o -path "$root/build" -o -path "$root/build-*" \) -prune -o -type f -size +1M -print | grep -q .; then
  echo "Unexpected file larger than 1 MiB; inspect before publishing" >&2
  exit 1
fi
