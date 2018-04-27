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

cp -r . "${OutputDir}$Name"

InputOptions=()
DepndencyFiles=(${MalterlibMeteorToolDependenciesFiles//;/ })

for FileEncoded in "${DepndencyFiles[@]}" ; do

	Decoded=(${FileEncoded//\|/ })

	File=${Decoded[0]}
	Destination=${Decoded[1]}

	InputOptions+=("Input:$File")

	if [[ "$Destination" != "" ]]; then
		MTool DiffCopy "$File" "${OutputDir}$Name/$Destination"
	else
		MTool DiffCopy "$File" "${OutputDir}$Name"
	fi
done

cd "${OutputDir}"

SysName=$(uname -s)
if [[ $SysName ==  Darwin* ]] ; then
	TarOptions="--disable-copyfile"
fi

tar $TarOptions -c "$Name" | gzip > "$OutputBundleTar"

md5 -q "$OutputBundleTar" > "$OutputBundleTar.md5" 

ExcludePatterns="*/bin;*/node_modules"
ExcludePatterns="$ExcludePatterns;*/.DS_Store"

MTool BuildDependencies "OutputFile=$DependencyFile" "Output:$OutputBundleTar" "Input:${BASH_SOURCE[0]}" "${InputOptions[@]}" "Find:$AppDir/*;RIF;33;$ExcludePatterns"

exit 0
