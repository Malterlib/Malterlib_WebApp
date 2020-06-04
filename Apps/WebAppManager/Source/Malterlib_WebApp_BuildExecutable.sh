#!/bin/bash

set -e

Action="$1"
OutputDir="$OutputDirectory"
Name="$MalterlibWebAppToolBuildName"
AppDir="$WebAppBuildDirectory"

mkdir -p "$OutputDir"

OutputBundleTar="${OutputDir}${Name}.tar.gz"

unset TOOLCHAINS
export PATH="/usr/local/sbin:/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin:$PATH"
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
DepndencyFiles=(${MalterlibWebAppToolDependenciesFiles//;/ })

if [[ "$PlatformFamily" != "Windows" ]] ; then
	function ConvertPath()
	{
		echo "$1"
	}
else
	function ConvertPath()
	{
		cygpath -m "$1"
	}
fi

for FileEncoded in "${DepndencyFiles[@]}" ; do

	Decoded=(${FileEncoded//\|/ })

	File=${Decoded[0]}
	Destination=${Decoded[1]}

	OptionRecursive=0
	OptionDirectory=0
	OptionDestinationFile=0

	if [[ "${File//\^}" != "$File" ]]; then
		File="${File//\^}"
		OptionRecursive=1
	fi

	if [[ "${File//\~}" != "$File" ]]; then
		File="${File//\~}"
		OptionDirectory=1
	fi

	if [[ "${Destination//\~}" != "$Destination" ]]; then
		Destination="${Destination//\~}"
		OptionDestinationFile=1
	fi

	if [[ "$Destination" != "" ]]; then
		DestinationDir="${OutputDir}$Name/$Destination"
	else
		DestinationDir="${OutputDir}$Name"
	fi

	echo "$File = $Destination"

	InputOptions+=("Input:`ConvertPath \"$File\"`")

	MTool DiffCopy "$File" "$DestinationDir" "" $OptionRecursive 0 $OptionDirectory $OptionDestinationFile
done

cd "${OutputDir}"

SysName=$(uname -s)
if [[ $SysName ==  Darwin* ]] ; then
	TarOptions="--disable-copyfile"
fi

tar $TarOptions -c "$Name" | gzip > "$OutputBundleTar"

if [[ "$PlatformFamily" != "Windows" ]] ; then
	md5 -q "$OutputBundleTar" > "$OutputBundleTar.md5"
else
	md5sum "$OutputBundleTar" | cut '-d ' -f 1 > "$OutputBundleTar.md5"
fi

ExcludePatterns="*/bin;*/node_modules"
ExcludePatterns="$ExcludePatterns;*/.DS_Store"

MTool BuildDependencies "OutputFile=`ConvertPath \"$DependencyFile\"`" "Output:`ConvertPath \"$OutputBundleTar\"`" "Input:`ConvertPath \"${BASH_SOURCE[0]}\"`" "${InputOptions[@]}" "Find:`ConvertPath \"$AppDir\"`/*;RIF;33;$ExcludePatterns"

exit 0
