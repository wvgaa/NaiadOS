#!/usr/bin/env bash
# Publishes the NaiadOS package repo: the newest package of every package
# directory goes to the x86_64 release of wvgaa/naiados-repo, signed, with a
# fresh database. Packages are uploaded before the database, so a user who
# syncs in between never gets a database that points to a missing file.
# The kernel build files are mirrored into the repo itself (GPL source).
set -euo pipefail

root=$(cd "$(dirname "$0")/.." && pwd)
repo=wvgaa/naiados-repo
tag=x86_64
db=naiados-repo
key=${GPGKEY:-$(sed -n 's/^GPGKEY="\(.*\)"/\1/p' ~/.makepkg.conf 2>/dev/null)}
[[ -n $key ]] || { echo "GPGKEY is not set (environment or ~/.makepkg.conf)"; exit 1; }

stage=$(mktemp -d)
trap 'rm -rf "$stage"' EXIT

# newest package per directory, debug packages left out
for dir in "$root"/kernel/*/ "$root"/NaiadTools/*/ "$root"/keyring/*/; do
    pkg=$(ls -t "$dir"*.pkg.tar.zst 2>/dev/null | grep -v -- '-debug-' | head -1 || true)
    [[ -n $pkg ]] || continue
    if [[ ! -f $pkg.sig ]]; then
        gpg --batch --yes --detach-sign --use-agent -u "$key" "$pkg"
    fi
    gpg --batch --verify "$pkg.sig" "$pkg" 2>/dev/null || { echo "bad signature: $pkg"; exit 1; }
    cp "$pkg" "$pkg.sig" "$stage/"
done

cd "$stage"
repo-add -q -s -v -k "$key" "$db.db.tar.gz" ./*.pkg.tar.zst
# release assets can't be symlinks
for f in "$db.db" "$db.db.sig" "$db.files" "$db.files.sig"; do
    [[ -L $f ]] && cp --remove-destination "$(readlink -f "$f")" "$f"
done

if ! gh release view "$tag" -R "$repo" >/dev/null 2>&1; then
    gh release create "$tag" -R "$repo" --title "$tag" --notes "Packages for the [naiados-repo] pacman repository."
fi
mapfile -t online < <(gh release view "$tag" -R "$repo" --json assets --jq '.assets[].name')

# package file names are unique per version, so existing ones are skipped
uploads=()
for f in ./*.pkg.tar.zst ./*.pkg.tar.zst.sig; do
    name=${f#./}
    printf '%s\n' "${online[@]}" | grep -qxF "$name" || uploads+=("$name")
done
if ((${#uploads[@]})); then
    gh release upload "$tag" -R "$repo" "${uploads[@]}"
fi
gh release upload "$tag" -R "$repo" --clobber "$db".db* "$db".files*

# remove what the database no longer lists (old versions)
for name in "${online[@]}"; do
    [[ -e $name ]] || gh release delete-asset "$tag" "$name" -R "$repo" -y
done

# GPL: PKGBUILDs, configs and hooks of the published kernels
src=$(mktemp -d)
trap 'rm -rf "$stage" "$src"' EXIT
gh repo clone "$repo" "$src" -- -q
for dir in "$root"/kernel/*/; do
    name=$(basename "$dir")
    mkdir -p "$src/kernel/$name"
    find "$src/kernel/$name" -type f -delete
    cp "$dir"PKGBUILD "$dir"config "$dir"*.hook "$dir"*.sh "$src/kernel/$name/"
done
git -C "$src" add -A
if ! git -C "$src" diff --cached --quiet; then
    git -C "$src" -c user.name=wvgaa -c user.email=326160773+wvgaa@users.noreply.github.com commit -q -m "Kernel sources for $(cd "$stage" && ls linux-*.pkg.tar.zst | sed 's/-x86_64.pkg.tar.zst//' | tr '\n' ' ')"
    git -C "$src" push -q
fi

echo "Published:"
ls "$stage"/*.pkg.tar.zst | xargs -n1 basename
if ((${#uploads[@]})); then
    echo "Uploaded: ${uploads[*]}"
else
    echo "No new packages, database refreshed."
fi
