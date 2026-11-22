clear
cat /etc/issue
. /etc/os-release
printf '    %s %s live\n\n' "$NAME" "$VERSION"
cat /etc/motd
