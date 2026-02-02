#!/bin/bash

set -e

Action="$1"
OutputDir="$OutputDirectory"
Name="$MalterlibWebAppToolBuildName"
AppDir="$WebAppBuildDirectory"

mkdir -p "$OutputDir"

OutputBundleTar="${OutputDir}${Name}.tar.zst"

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

if [[ "$Action" == "Rebuild" || "$Action" == "Clean" ]]; then
	if [ -e "$OutputBundleTar" ]; then
		rm -rf "$OutputBundleTar"
	fi
fi

if [ "$Action" == "Clean" ]; then
	exit 0
fi

if [ -e "$ScriptDependencyFile" ]; then
	MTool CheckDependencies Verbose=true "Directory=$OutputDir"
fi

if [ -e "$OutputBundleTar" ] && [ -e "$ScriptDependencyFile" ]; then
	echo Bundle is up to date. To force rebuild:
	echo rm -f \"$OutputBundleTar\"
	exit 0
fi

echo "Building $Name bundle"
rm -rf "${OutputDir}$Name"
cd "$AppDir"

cp -r . "${OutputDir}$Name"

cd "${OutputDir}"

SysName=$(uname -s)
if [[ $SysName ==  Darwin* ]] ; then
	TarOptions="--disable-copyfile"
fi

bsdtar $TarOptions -caf "$OutputBundleTar" "$Name"

if [[ "$HostPlatformFamily" != "Windows" ]] ; then
	if [[ "$HostPlatformFamily" == "macOS" ]] ; then
		md5 -q "$OutputBundleTar" > "$OutputBundleTar.md5"
	else
		md5sum "$OutputBundleTar" | cut '-d ' -f 1 > "$OutputBundleTar.md5"
	fi

	function ConvertPath()
	{
		echo "$1"
	}
else
	md5sum "$OutputBundleTar" | cut '-d ' -f 1 > "$OutputBundleTar.md5"
	function ConvertPath()
	{
		cygpath -m "$1"
	}
fi

ExcludePatterns="*/bin;*/node_modules"
ExcludePatterns="$ExcludePatterns;*/.DS_Store"

MTool BuildDependencies "OutputFile=`ConvertPath \"$ScriptDependencyFile\"`" "Output:`ConvertPath \"$OutputBundleTar\"`" "Input:`ConvertPath \"${BASH_SOURCE[0]}\"`" "Find:`ConvertPath \"$AppDir\"`/*;RIF;33;$ExcludePatterns"

exit 0
