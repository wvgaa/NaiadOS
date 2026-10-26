#!/usr/bin/env bash
# Creates the NaiadOS package signing key (run once) and writes the public
# part into keyring/naiados-keyring. Also trusts it in this machine's pacman
# keyring, which mkarchiso uses when it builds the ISO.
set -euo pipefail

root=$(cd "$(dirname "$0")/.." && pwd)
uid=${1:?usage: make-signing-key.sh "NaiadOS Package Signing <address>"}
dir="$root/keyring/naiados-keyring"

gpg --quick-generate-key "$uid" ed25519 sign never
fpr=$(gpg --list-secret-keys --with-colons "$uid" | awk -F: '/^fpr/ {print $10; exit}')

gpg --export "$fpr" > "$dir/naiados.gpg"
echo "$fpr:4:" > "$dir/naiados-trusted"

sudo pacman-key --add "$dir/naiados.gpg"
sudo pacman-key --lsign-key "$fpr"

echo
echo "Key: $fpr"
echo "Add to ~/.makepkg.conf:  GPGKEY=\"$fpr\""
echo "Back up the secret key:  gpg --export-secret-keys --armor $fpr > naiados-secret.asc"
