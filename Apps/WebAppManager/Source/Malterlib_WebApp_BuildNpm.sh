#!/bin/bash

set -e

ScriptDir="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

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

function LockFile
{
	if [ "$#" -ne 1 ]; then
		echo 'usage: LockFile [LOCKFILENAME]' 1>&2
		return 2
	fi
	LOCKFILE="$1"

	echo "$$" >"$LOCKFILE.$$"
	if ! ln "$LOCKFILE.$$" "$LOCKFILE" 2>/dev/null; then
		PID=`head -1 "$LOCKFILE"`
		if [ -z "$PID" ]; then
		   rm -f "$LOCKFILE"
		else
		   kill -0 "$PID" 2>/dev/null || rm -f "$LOCKFILE"
		fi

		if ! ln "$LOCKFILE.$$" "$LOCKFILE" 2>/dev/null; then
		   rm -f "$LOCKFILE.$$"
		   return 1
		fi
	fi

	rm -f "$LOCKFILE.$$"
	trap 'rm -f "$LOCKFILE"' EXIT

	return 0
}

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
export PATH="/opt/homebrew/sbin:/opt/homebrew/bin:/usr/local/sbin:/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin"

if [[ "$MalterlibWebAppHostNodePackagePath" != "" ]] && [[ "$MalterlibWebAppToolUseSystemNode" != "true" ]]; then
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
	fi

	pushd "$NodeDirectory" > /dev/null

	bsdtar $TarExtractOptions --no-same-owner --strip-components=1 -xf "$MalterlibWebAppHostNodePackagePath"
	sed -i -e "s/build_file_path, 'rU'/build_file_path, 'r'/g" "$NodeDirectory/lib/node_modules/npm/node_modules/node-gyp/gyp/pylib/gyp/input.py"

	export PATH="$PWD/bin:$PWD:$PATH"
	popd > /dev/null
fi

echo "Building $Name bundle"
rm -rf "${OutputDir}$Name"
cd "$AppDir"
mkdir -p "$AppDir/build-lock"

SECONDS=0
LastSeconds=-1
while ! LockFile "$AppDir/build-lock/build.lock"; do
	ThisSeconds=$SECONDS
	if [[ "$ThisSeconds" != "$LastSeconds" ]] && [[ "$(($ThisSeconds % 10))" == "0" ]]; then
		echo Waiting for other build in $AppDir to finish: $ThisSeconds s
	else
		sleep 1
	fi
	LastSeconds=$ThisSeconds
done

export NPM_CONFIG_PROGRESS=false

function RunNpmBuild()
{
	if [[ "$PlatformFamily" == "Linux" ]] && [[ "$HostPlatformFamily" == "macOS" ]]; then
		rm -rf node_modules
		npm ci
		npm run $1
		rm -rf node_modules

		container system start --enable-kernel-install
		container image pull ubuntu:24.04

		PathHash=`echo "$OutputDir" | sha256sum --quiet`
		ContainerName="${Name}-${PathHash::8}"

		TempDir="${HOME}/.malterlib/local/container/$ContainerName"
		mkdir -p "$TempDir"

		rm -rf "$TempDir/"*
		cp -r "." "$TempDir/App"
		cp "$ScriptDir/Malterlib_WebApp_ContainerNpmInstall.sh" "$TempDir/"

		if [[ `container inspect "$ContainerName"` == "[]" ]]; then
			ContainerArchitecture="$Architecture"
			if [[ "$Architecture" == "x64" ]]; then
				ContainerArchitecture="amd64"
			fi

			container create --name "$ContainerName" -m 1G -c 4 -a $ContainerArchitecture -v "$TempDir:/opt/work" ubuntu:24.04 --entrypoint /bin/bash -- -c "trap : TERM INT; sleep infinity & wait"
		fi

		if [[ `container inspect "$ContainerName" | jq -r '.[0].["status"]'` != "running" ]]; then
			container start "$ContainerName"
		fi

		container exec --cwd /opt/work --env "NpmInstallInContainer=true" --env "Architecture=$Architecture" -- "$ContainerName" "/opt/work/Malterlib_WebApp_ContainerNpmInstall.sh" "$TempOutputBundleTarRemote" "$TempXNodePackageRemote" "$MeteorBuildName"
		container stop "$ContainerName"

		cp -r "$TempDir/App/node_modules" .
	else
		if [[ "$MalterlibWebAppManagerDevDepenencesInPackage" == "true" ]]; then
			# Dev mode - skip clean reinstalls and only reinstall if package files changed

			# Calculate hash of package files
			if [[ "$PlatformFamily" != "Windows" ]]; then
				CurrentHash=$(cat package.json package-lock.json 2>/dev/null | md5 -q)
			else
				CurrentHash=$(cat package.json package-lock.json 2>/dev/null | md5sum | cut -d' ' -f1)
			fi

			HashFile="node_modules/.package-hash"
			NeedInstall=false

			# Check if hash file exists and compare
			if [[ ! -f "$HashFile" ]]; then
				NeedInstall=true
				echo "Hash file not found, running npm ci"
			else
				StoredHash=$(cat "$HashFile")
				if [[ "$CurrentHash" != "$StoredHash" ]]; then
					NeedInstall=true
					echo "Package files changed, running npm ci"
				else
					echo "Dependencies unchanged, skipping npm ci"
				fi
			fi

			# Install dependencies if needed
			if [[ "$NeedInstall" == "true" ]]; then
				rm -rf node_modules
				npm ci
				# Store the hash
				echo "$CurrentHash" > "$HashFile"
			fi

			# Run the build
			npm run $1
		else
			rm -rf node_modules
			npm ci
			npm run $1
			rm -rf node_modules
			npm ci --production
		fi
	fi
}

if [[ "$NpmBuildType" == "Start" ]]; then
	SourceDir=.
	RunNpmBuild prestart
elif [[ "$NpmBuildType" == "Compile" ]]; then
	SourceDir=build
	RunNpmBuild compile
elif [[ "$NpmBuildType" == "Build" ]]; then
	SourceDir=build
	RunNpmBuild build
else
	echo "Unknown NpmBuildType: '$NpmBuildType'"
	exit 1
fi

rm -f lastrun.md5

export PATH="$OldPath"

SysName=$(uname -s)
if [[ $SysName ==  Darwin* ]] ; then
	TarOptions="--disable-copyfile"
fi

cd "$SourceDir"

# Build exclusion patterns for archive
ExcludeArgs=""
ExcludeArgs="$ExcludeArgs --exclude .git"
ExcludeArgs="$ExcludeArgs --exclude .DS_Store"
ExcludeArgs="$ExcludeArgs --exclude '*.log'"
ExcludeArgs="$ExcludeArgs --exclude .env"
ExcludeArgs="$ExcludeArgs --exclude .env.*"
ExcludeArgs="$ExcludeArgs --exclude build.lock"
ExcludeArgs="$ExcludeArgs --exclude build-lock"

bsdtar $TarOptions $ExcludeArgs -s "|^\./|$Name/|" -caf "$OutputBundleTar" .

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
ExcludePatterns="$ExcludePatterns;*/.DS_Store;*/build-lock"

MTool BuildDependencies "OutputFile=`ConvertPath \"$ScriptDependencyFile\"`" "Output:`ConvertPath \"$OutputBundleTar\"`" "Input:`ConvertPath \"${BASH_SOURCE[0]}\"`" "Find:`ConvertPath \"$AppDir\"`/*;RIF;33;$ExcludePatterns"

exit 0
