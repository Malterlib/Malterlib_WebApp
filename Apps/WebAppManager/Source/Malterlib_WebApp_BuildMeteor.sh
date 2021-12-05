#!/bin/bash

set -ex

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

Action="$1"
OutputDir="$OutputDirectory"
Name="$MalterlibWebAppToolBuildName"
MeteorDir="$WebAppBuildDirectory"
SharedPackagesDir="$MalterlibWebAppMeteorSharedPackages"
PlatformFamily="$PlatformFamily"
Architecture="$Architecture"
MeteorCheckedOutPath="$MalterlibWebAppMeteorCheckedOutPath"
MeteorDebugPackage="$MalterlibWebAppMeteorDebugBundle"
NodePackage="$MalterlibWebAppNodePackagePath"

OutputBundleTar="${OutputDir}${Name}.tar.gz"

export METEOR_PACKAGE_DIRS="$SharedPackagesDir"

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

OldPath="$PATH"
if [[ "$MeteorOverridePath" == "true" ]]; then
	export PATH="/opt/homebrew/sbin:/opt/homebrew/bin:/usr/local/sbin:/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin"
fi

if [ -n "$MeteorCheckedOutPath" ]; then
	PATH="$MeteorCheckedOutPath:$PATH"
fi
ls -laF "$MeteorDir"

echo Building meteor bundle
rm -rf "${OutputDir}$Name"
cd "$MeteorDir"

rm -rf .meteor/local/plugin-cache/less/local

if [[ "$PlatformFamily" == "Windows" ]]; then
	if [[ "$Architecture" == "x64" ]]; then
		METEOR_ARCH="os.windows.x86_64"
	else
		echo "Unknown meteor arch for PlatformFamily: $PlatformFamily and Architecture: $Architecture"
		exit 1
	fi
elif [[ "$PlatformFamily" == "Linux" ]]; then
	if [[ "$Architecture" == "x64" ]]; then
		METEOR_ARCH="os.linux.x86_64"
	elif [[ "$Architecture" == "arm64" ]]; then
		METEOR_ARCH="os.linux.aarch64"
	else
		echo "Unknown meteor arch for PlatformFamily: $PlatformFamily and Architecture: $Architecture"
		exit 1
	fi
elif [[ "$PlatformFamily" == "OSX" ]]; then
	if [[ "$Architecture" == "x64" ]]; then
		METEOR_ARCH="os.osx.x86_64"
	elif [[ "$Architecture" == "arm64" ]]; then
		METEOR_ARCH="os.osx.arm64"
	else
		echo "Unknown meteor arch for PlatformFamily: $PlatformFamily and Architecture: $Architecture"
		exit 1
	fi
else
	echo "Unknown meteor arch for PlatformFamily: $PlatformFamily"
	exit 1
fi

export NPM_CONFIG_PROGRESS=false

rm -rf node_modules
$MeteorCommand npm ci --production

mkdir -p "${OutputDir}bundle"

if [ "$MeteorDebugPackage" == "true" ]; then
	echo Building meteor bundle with debug
	$MeteorCommand build "$OutputDir" --server-only --debug --architecture "$METEOR_ARCH" --directory
else
	$MeteorCommand build "$OutputDir" --server-only --architecture "$METEOR_ARCH" --directory
fi

export PATH="$OldPath"

SysName=$(uname -s)
if [[ $SysName ==  Darwin* ]] ; then
	TarOptions="--disable-copyfile"
fi

mv "${OutputDir}bundle" "${OutputDir}$Name"
cd "$OutputDir"
tar $TarOptions -czf "$OutputBundleTar" "$Name"

if [[ "$PlatformFamily" == "Linux" ]] ; then
	if [[ "$BUILDSERVER" == "TRUE" ]] ; then
		pushd "$ScriptDir"
		MTool BuildServerTool Tool=MeteorBuild_Ubuntu1604 "$ScriptDir/Malterlib_WebApp_BuildMeteorNpmInstall.sh" "$OutputBundleTar" "$NodePackage" "$Name"
		popd
	else
		echo npm ci not supported for Linux
	fi
else
	"$ScriptDir/Malterlib_WebApp_BuildMeteorNpmInstall.sh" "$OutputBundleTar" "$NodePackage" "$Name"
fi

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

ExcludePatterns="*/.meteor/local"
ExcludePatterns="$ExcludePatterns;*/node_modules"
ExcludePatterns="$ExcludePatterns;*/.example"
ExcludePatterns="$ExcludePatterns;*/.npm"
ExcludePatterns="$ExcludePatterns;*/.git"
ExcludePatterns="$ExcludePatterns;*/.DS_Store"
ExcludePatterns="$ExcludePatterns;*/*.MRepoState"

DependencyCommands=("OutputFile=`ConvertPath \"$ScriptDependencyFile\"`" "Output:`ConvertPath \"$OutputBundleTar\"`" "Input:`ConvertPath \"${BASH_SOURCE[0]}\"`" "Input:`ConvertPath \"$ScriptDir/Malterlib_WebApp_BuildMeteorNpmInstall.sh\"`" "Find:`ConvertPath \"$MeteorDir\"`/*;RIF;33;$ExcludePatterns")
echo $DependencyCommands
if [[ "$SharedPackagesDir" != "" ]]; then
	DependencyCommands+=(@"Find:`ConvertPath \"$SharedPackagesDir\"`/*;RIF;33;$ExcludePatterns")
fi

echo "${DependencyCommands[@]}"

MTool BuildDependencies "${DependencyCommands[@]}"

exit 0
