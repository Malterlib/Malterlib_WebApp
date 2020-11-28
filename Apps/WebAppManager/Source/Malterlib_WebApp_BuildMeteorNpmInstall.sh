#!/bin/bash

set -e

ScriptDir="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

unset TOOLCHAINS
export PATH="/opt/homebrew/sbin:/opt/homebrew/bin:/usr/local/sbin:/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin:$PATH"
unset MACOSX_DEPLOYMENT_TARGET
unset SDKROOT
unset PRODUCT_SPECIFIC_LDFLAGS
unset OTHER_CFLAGS_ONLY
unset CC
unset CLANG
unset CPLUSPLUS
unset LD
unset LDPLUSPLUS

Package="$1"
NodePackage="$2"
Name="$3"

TempDirectory="`mktemp -d`"
NodeDirectory="`mktemp -d`"

SysName=$(uname -s)
if [[ $SysName ==  Darwin* ]] ; then
	TarOptions="--disable-copyfile"
else
	TarExtractOptions="--pax-option=delete=SCHILY.*,delete=LIBARCHIVE.*"
fi

OldPath="$PATH"
export PATH="/opt/homebrew/sbin:/opt/homebrew/bin:/usr/local/sbin:/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin"

if [[ "$NodePackage" != "" ]]; then
	pushd "$NodeDirectory" > /dev/null
	tar $TarExtractOptions --no-same-owner --strip-components=1 -xf "$NodePackage"
	export PATH="$PWD/bin:$PATH"
	popd > /dev/null
fi

pushd "$TempDirectory" > /dev/null

tar $TarExtractOptions --no-same-owner -xf "$Package"

pushd "$Name/programs/server/" > /dev/null

chmod -R u+w .

export NPM_CONFIG_PROGRESS=false

if ! npm install &>"$TempDirectory/npmerror.log" ; then
	cat "$TempDirectory/npmerror.log"
	exit 1
fi

chmod -R -w .

export PATH="$OldPath"

popd > /dev/null

touch "$Name/.installed"
tar $TarOptions -czf "$Package" "$Name"
popd > /dev/null

chmod -R u+w "$TempDirectory"
rm -rf "$TempDirectory"
rm -rf "$NodeDirectory"

exit 0
