#!/usr/bin/env bash
# Builds a signed .appx for sideloading.
#
# Usage: package.sh <build_dir> <out_dir>
#
# Environment:
#   MAKEMSIX       makemsix built with MSIX_PACK=on (default: makemsix on PATH)
#   VCLIBS_APPX    Microsoft.VCLibs.x64.14.00.appx, copied next to the package
#   CERT_PFX       signing certificate; its subject must equal the manifest
#                  Publisher. When unset, a test certificate is created once in
#                  ~/.local/share/sdl-uwp-onboarding-cert and reused.
#   CERT_PASSWORD  password of CERT_PFX (default: none)
set -euo pipefail

if [ $# -ne 2 ]; then
  echo "usage: $0 <build_dir> <out_dir>" >&2
  exit 2
fi
build_dir=$(realpath "$1")
out_dir=$(realpath -m "$2")
repo=$(realpath "$(dirname "$0")/..")
makemsix=${MAKEMSIX:-makemsix}

for f in onboarding.exe SDL3.dll; do
  if [ ! -f "$build_dir/$f" ]; then
    echo "missing $build_dir/$f" >&2
    exit 1
  fi
done

layout="$out_dir/layout"
rm -rf "$layout"
mkdir -p "$layout/Logos"
cp "$build_dir/onboarding.exe" "$build_dir/SDL3.dll" "$repo/packaging/AppxManifest.xml" "$layout/"

logo() {
  magick -size "$2" xc:'#12141c' -fill white -gravity center \
    -pointsize "$3" -annotate 0 'Onboarding' "$layout/Logos/$1"
}
logo StoreLogo.png 50x50 8
logo Square44x44Logo.png 44x44 7
logo Square150x150Logo.png 150x150 24
logo SplashScreen.png 620x300 64

# Reusing one test certificate means a device only has to trust it once.
cert_dir="$HOME/.local/share/sdl-uwp-onboarding-cert"
cert=${CERT_PFX:-$cert_dir/test-cert.pfx}
cert_password=${CERT_PASSWORD:-}
if [ -z "${CERT_PFX:-}" ] && [ ! -f "$cert" ]; then
  mkdir -p "$cert_dir"
  chmod 700 "$cert_dir"
  (umask 077
   openssl req -x509 -newkey rsa:2048 -nodes -days 3650 -subj "/CN=SdlUwpOnboarding Test" \
     -addext "extendedKeyUsage=codeSigning" -addext "basicConstraints=CA:FALSE" \
     -addext "keyUsage=digitalSignature" \
     -keyout "$cert_dir/test-cert.key" -out "$cert_dir/test-cert.cer" 2>/dev/null
   openssl pkcs12 -export -inkey "$cert_dir/test-cert.key" -in "$cert_dir/test-cert.cer" \
     -out "$cert" -passout pass:)
fi

# Installation fails unless the certificate subject equals Identity/@Publisher.
publisher=$(sed -n 's/.*<Identity [^>]*Publisher="\([^"]*\)".*/\1/p' "$layout/AppxManifest.xml")
subject=$(openssl pkcs12 -in "$cert" -passin "pass:$cert_password" -nokeys -clcerts 2>/dev/null \
  | openssl x509 -noout -subject | sed 's/^subject= *//')
if [ "$subject" != "$publisher" ]; then
  echo "certificate subject '$subject' does not match manifest Publisher '$publisher'" >&2
  exit 1
fi

unsigned="$out_dir/onboarding-unsigned.appx"
signed="$out_dir/onboarding.appx"
rm -f "$unsigned" "$signed"
"$makemsix" pack -d "$layout" -p "$unsigned"
osslsigncode sign -pkcs12 "$cert" -pass "$cert_password" -in "$unsigned" -out "$signed"
rm -f "$unsigned"

if [ -n "${VCLIBS_APPX:-}" ]; then
  cp "$VCLIBS_APPX" "$out_dir/"
fi

echo "$signed"
