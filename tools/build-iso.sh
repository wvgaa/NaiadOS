#!/usr/bin/env bash
# Builds the live ISO into ~/naiadiso-output. Copies the kernel build files
# in first, so "Compile" in the installer uses the same version and config
# as the prebuilt kernels.
set -euo pipefail

root=$(cd "$(dirname "$0")/.." && pwd)
dest="$root/naiadiso/airootfs/usr/share/naiados/kernel"
out=~/naiadiso-output
work=/var/tmp/naiadiso-work

rm -rf "$dest"
for dir in "$root"/kernel/*/; do
    name=$(basename "$dir")
    mkdir -p "$dest/$name"
    cp "$dir"PKGBUILD "$dir"config "$dir"*.hook "$dir"*.sh "$dest/$name/"
done

mkdir -p "$out"
sudo rm -rf "$work"
sudo mkarchiso -v -w "$work" -o "$out" "$root/naiadiso"
sudo rm -rf "$work"
sudo chown "$(id -u):$(id -g)" "$out"/naiados-*.iso
ls -lt "$out"/naiados-*.iso | head -1
