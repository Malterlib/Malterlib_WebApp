#!/bin/bash

set -e

Action="$1"
OutputDir="$OutputDirectory"
Name="$MalterlibMeteorToolBuildName"
AppDir="$MeteorBuildDirectory"

mkdir -p "$OutputDir"

OutputBundleTar="${OutputDir}${Name}.tar.gz"

export PATH=/usr/local/bin:$PATH

if [[ "$Action" == "Rebuild" || "$Action" == "Clean" ]]; then
	if [ -e "$OutputBundleTar" ]; then
		rm -rf "$OutputBundleTar"
	fi
fi

if [ "$Action" == "Clean" ]; then
	exit 0
fi

DependencyFile=${OutputDir}$Name.MalterlibDependency

if [ -e "$DependencyFile" ]; then
	MTool CheckDependencies Verbose=true "Directory=$OutputDir"
fi

if [ -e "$OutputBundleTar" ] && [ -e "$DependencyFile" ]; then
	echo Bundle is up to date. To force rebuild:
	echo rm -f \"$OutputBundleTar\"
	exit 0
fi

echo "Building $Name bundle"
rm -rf "${OutputDir}$Name"
cd "$AppDir"

export NPM_CONFIG_PROGRESS=false

npm install --production
npm run prestart

cp -r . "${OutputDir}$Name"

cd "${OutputDir}"

SysName=$(uname -s)
if [[ $SysName ==  Darwin* ]] ; then
	TarOptions="--disable-copyfile"
fi

tar $TarOptions -czf "$OutputBundleTar" "$Name"



if [[ "$PlatformFamily" != "Windows" ]] ; then
	md5 -q "$OutputBundleTar" > "$OutputBundleTar.md5"
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

MTool BuildDependencies "OutputFile=`ConvertPath \"$DependencyFile\"`" "Output:ConvertPath `\"$OutputBundleTar\"`" "Input:ConvertPath `\"${BASH_SOURCE[0]}\"`" "Find:`ConvertPath \"$AppDir\"`/*;RIF;33;$ExcludePatterns"

exit 0
