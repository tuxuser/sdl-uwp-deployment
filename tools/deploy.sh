#!/usr/bin/env bash
# Installs an .appx and its dependencies on a Dev Mode Xbox (or a Windows PC
# with Device Portal) and launches it.
#
# Usage: deploy.sh <portal-url> <app.appx> [dependency.appx ...]
#   e.g. deploy.sh https://192.168.1.50:11443 build/package/onboarding.appx \
#          build/package/Microsoft.VCLibs.x64.14.00.appx
#
# Environment: WDP_USER, WDP_PASSWORD when the portal requires credentials.
set -euo pipefail

if [ $# -lt 2 ]; then
  echo "usage: $0 <portal-url> <app.appx> [dependency.appx ...]" >&2
  exit 2
fi
portal=${1%/}
app=$2
shift 2

jar=$(mktemp)
trap 'rm -f "$jar"' EXIT
auth=()
if [ -n "${WDP_USER:-}" ]; then
  auth=(-u "$WDP_USER:${WDP_PASSWORD:-}")
fi
# The portal uses a self-signed certificate.
curl_portal() { curl -sk ${auth[@]+"${auth[@]}"} "$@"; }

# Any GET sets the CSRF cookie that POST and DELETE requests must echo.
curl_portal -c "$jar" -o /dev/null --max-time 120 "$portal/api/os/info"
token=$(awk '$6 == "CSRF-Token" { print $7; exit }' "$jar")

# The portal identifies each package by its uploaded file name.
form=(-F "$(basename "$app")=@$app;filename=$(basename "$app")")
for dependency in "$@"; do
  form+=(-F "$(basename "$dependency")=@$dependency;filename=$(basename "$dependency")")
done
echo "uploading $(basename "$app") and $# dependencies"
curl_portal -b "$jar" -H "X-CSRF-Token: $token" --max-time 3600 -f -o /dev/null "${form[@]}" \
  "$portal/api/app/packagemanager/package?package=$(basename "$app")"

# 204 while the install runs, then 200 with the result.
state=""
for _ in $(seq 1 600); do
  code=$(curl_portal -b "$jar" --max-time 120 -o "$jar.state" -w '%{http_code}' \
    "$portal/api/app/packagemanager/state" || true)
  if [ "$code" = 200 ]; then
    state=$(cat "$jar.state")
    break
  fi
  sleep 2
done
rm -f "$jar.state"
echo "$state"
if ! grep -q '"Success" *: *true' <<<"$state"; then
  echo "install failed or did not finish" >&2
  exit 1
fi

identity=$(unzip -p "$app" AppxManifest.xml | grep -o '<Identity [^>]*>')
name=$(sed -n 's/.*Name="\([^"]*\)".*/\1/p' <<<"$identity")
version=$(sed -n 's/.*Version="\([^"]*\)".*/\1/p' <<<"$identity")
full_name=$(curl_portal --max-time 120 "$portal/api/app/packagemanager/packages" |
  grep -o "\"PackageFullName\" *: *\"${name}_${version}_[^\"]*\"" | head -1 | sed 's/.*: *"\(.*\)"/\1/')
relative_id=$(curl_portal --max-time 120 "$portal/api/app/packagemanager/packages" |
  grep -o "\"PackageRelativeId\" *: *\"${name}_[^\"]*\"" | head -1 | sed 's/.*: *"\(.*\)"/\1/')
if [ -z "$full_name" ] || [ -z "$relative_id" ]; then
  echo "installed package not found" >&2
  exit 1
fi
echo "launching $relative_id"
# Base64 may contain '+', which a query string would read as a space.
appid=$(printf '%s' "$relative_id" | base64 -w0 | sed 's/+/%2B/g; s/\//%2F/g; s/=/%3D/g')
package=$(printf '%s' "$full_name" | base64 -w0 | sed 's/+/%2B/g; s/\//%2F/g; s/=/%3D/g')
curl_portal -b "$jar" -H "X-CSRF-Token: $token" -H "Content-Length: 0" -X POST --max-time 120 -f \
  -o /dev/null "$portal/api/taskmanager/app?appid=$appid&package=$package"
