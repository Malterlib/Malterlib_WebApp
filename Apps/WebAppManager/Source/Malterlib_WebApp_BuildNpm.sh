#!/bin/bash

set -e

Action="$1"
OutputDir="$OutputDirectory"
Name="$MalterlibWebAppToolBuildName"
AppDir="$WebAppBuildDirectory"

mkdir -p "$OutputDir"

OutputBundleTar="${OutputDir}${Name}.tar.gz"

export PATH=/usr/local/bin:$PATH

if [[ "$MalterlibWebAppHostNodePackagePath" != "" ]]; then
	NodeDirectory="`mktemp -d`"
	function clean_up () {
	    ARG=$?
		rm -rf "$NodeDirectory"
	    exit $ARG
	}
	trap clean_up EXIT

	SysName=$(uname -s)
	if [[ $SysName ==  Darwin* ]] ; then
		TarOptions="--disable-copyfile"
	else
		TarExtractOptions="--pax-option=delete=SCHILY.*,delete=LIBARCHIVE.*"
	fi

	pushd "$NodeDirectory" > /dev/null

	tar $TarExtractOptions --no-same-owner --strip-components=1 -xf "$MalterlibWebAppHostNodePackagePath"
	export PATH="$PWD/bin:$PATH"
	popd > /dev/null
fi

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

if [[ "$NpmBuildType" == "Start" ]]; then
	SourceDir=.
	rm -rf node_modules
	npm ci --production
	npm run prestart
elif [[ "$NpmBuildType" == "Compile" ]]; then
	SourceDir=build
	rm -rf build
	rm -rf node_modules
	npm ci
	npm run compile
elif [[ "$NpmBuildType" == "Build" ]]; then
	SourceDir=build
	rm -rf build
	rm -rf node_modules
	npm ci
	npm run build
else
	echo "Unknown NpmBuildType: '$NpmBuildType'"
	exit 1
fi

cp -r $SourceDir "${OutputDir}$Name"

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

MTool BuildDependencies "OutputFile=`ConvertPath \"$DependencyFile\"`" "Output:`ConvertPath \"$OutputBundleTar\"`" "Input:`ConvertPath \"${BASH_SOURCE[0]}\"`" "Find:`ConvertPath \"$AppDir\"`/*;RIF;33;$ExcludePatterns"

exit 0
