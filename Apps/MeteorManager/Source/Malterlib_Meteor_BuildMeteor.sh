#!/bin/bash

set -e

ScriptDir="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

unset SDKROOT

Action="$1"
OutputDir="$OutputDirectory"
Name="$MalterlibMeteorToolBuildName"
MeteorDir="$MeteorBuildDirectory"
SharedPackagesDir="$MalterlibMeteorSharedPackages"
PlatformFamily="$PlatformFamily"
MeteorGitCheckout="$MalterlibMeteorGitCheckout"
MeteorDebugPackage="$MalterlibMeteorDebugBundle"
NodePackage="$MalterlibMeteorNodePackagePath"

OutputBundleTar="${OutputDir}${Name}.tar.gz"

export PATH=/usr/local/bin:$PATH
export METEOR_PACKAGE_DIRS="$SharedPackagesDir"

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

if [ -n "$MeteorGitCheckout" ]; then
	echo Using Meteor from git checkout
	if [ ! -e ./.latestmeteor ]; then
		git clone https://github.com/meteor/meteor ./.latestmeteor
	fi
	pushd ./.latestmeteor
	git clean -df
	git reset --hard HEAD
	git remote add hansoft https://github.com/Hansoft/meteor.git 2> /dev/null || true
	git fetch ${MeteorGitCheckout/\// }
	git checkout "$MeteorGitCheckout"
	git submodule update --init --recursive
	popd
	PATH="$PWD/.latestmeteor:$PATH"
fi

echo Building meteor bundle
rm -rf "${OutputDir}$Name"
cd "$MeteorDir"

if [ "$PlatformFamily" == "Linux" ]; then
	METEOR_ARCH="os.linux.x86_64"
else
	METEOR_ARCH="os.osx.x86_64"
fi

export NPM_CONFIG_PROGRESS=false

meteor npm install --production

if [ "$MeteorDebugPackage" == "true" ]; then
	echo Building meteor bundle with debug
	meteor build "$OutputDir" --server-only --debug --architecture "$METEOR_ARCH" --directory
else
	meteor build "$OutputDir" --server-only --architecture "$METEOR_ARCH" --directory
fi

mv "${OutputDir}bundle" "${OutputDir}$Name"
cd "$OutputDir"
gnutar -c "$Name" | gzip > "$OutputBundleTar"

if [[ "$PlatformFamily" == "Linux" ]] ; then
	if [[ "$BUILDSERVER" == "TRUE" ]] ; then
		pushd "$ScriptDir"
		MTool BuildServerTool Tool=MeteorBuild_Ubuntu1604 "$ScriptDir/Malterlib_Meteor_BuildMeteorNpmInstall.sh" "$OutputBundleTar" "$NodePackage" "$Name"
		popd
	else
		echo npm install not supported for Linux
	fi
else
	"$ScriptDir/Malterlib_Meteor_BuildMeteorNpmInstall.sh" "$OutputBundleTar" "$NodePackage" "$Name"
fi

ExcludePatterns="*/.meteor/local"
ExcludePatterns="$ExcludePatterns;*/tests/jasmine/server/unit/package-stubs.js"
ExcludePatterns="$ExcludePatterns;*/tests/jasmine/server/unit/packageMocksSpec.js"
ExcludePatterns="$ExcludePatterns;*/packages/tests-proxy"
ExcludePatterns="$ExcludePatterns;*/node_modules"
ExcludePatterns="$ExcludePatterns;*/.npm"
ExcludePatterns="$ExcludePatterns;*/.git"
ExcludePatterns="$ExcludePatterns;*/.DS_Store"

MTool BuildDependencies "OutputFile=$DependencyFile" "Output:$OutputBundleTar" "Input:${BASH_SOURCE[0]}" "Input:$ScriptDir/Malterlib_Meteor_BuildMeteorNpmInstall.sh" "Find:$MeteorDir/*;RIF;33;$ExcludePatterns" "Find:$SharedPackagesDir/*;RIF;33;$ExcludePatterns"

exit 0
