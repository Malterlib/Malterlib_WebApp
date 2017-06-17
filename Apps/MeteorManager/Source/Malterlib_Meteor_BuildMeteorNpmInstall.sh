#!/bin/bash

set -e

ScriptDir="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

export PATH="/usr/local/bin:$PATH"

export SDKROOT=

Package="$1"
NodePackage="$2"
Name="$3"

TempDirectory="`mktemp -d`"
NodeDirectory="`mktemp -d`"

if [ "$(uname)" == "Darwin" ]; then
	TarExecutable="gnutar"
else
	TarExecutable="tar"
fi

pushd "$NodeDirectory" > /dev/null

tar --no-same-owner --strip-components=1 -xf "$NodePackage"
export PATH="$PWD/bin:$PATH"
popd > /dev/null

pushd "$TempDirectory" > /dev/null

tar --no-same-owner -xf "$Package"

pushd "$Name/programs/server/" > /dev/null

export NPM_CONFIG_PROGRESS=false

if ! npm install &>"$TempDirectory/npmerror.log" ; then
	cat "$TempDirectory/npmerror.log"
	exit 1
fi

popd > /dev/null

touch "$Name/.installed"
$TarExecutable -c "$Name" | gzip > "$Package"
popd > /dev/null

exit 0
