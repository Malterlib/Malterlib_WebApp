#!/usr/bin/env bash

set -e

OutputDir="$1"
IntermediateDir="$2"

if [[ "$OutputDir" == "" ]]; then
	echo "No output dir specified"
	exit 1
fi

if [[ "$IntermediateDir" == "" ]]; then
	IntermediateDir="/opt/CompiledFiles/BuildNode"
	rm -rf "$IntermediateDir"
fi

SysName=$(uname -s)
ProcessorArch=$(uname -m)

if [[ $SysName ==  Darwin* ]] ; then
	NodePlatform=darwin
	OutputPlatform=macOS
	NumCPUs=`getconf _NPROCESSORS_ONLN`
	BuildPlatform=macOS
	StripCommand="strip -u -r"
	function RunMD5()
	{
		md5 -q "$1"
	}
elif [[ $SysName ==  Linux* ]] ; then
	NodePlatform=linux
	OutputPlatform=Linux
	NumCPUs=`getconf _NPROCESSORS_ONLN`
	ExtraBoringSSLFlags="-fPIC"
	BuildPlatform=Linux
	StripCommand="strip --strip-unneeded"
	function RunMD5()
	{
		md5sum "$1" | cut '-d ' -f 1
	}
else
	echo "Couldn't detect system"
	exit 1
fi

if [[ $ProcessorArch == i*86 ]] ; then
	BuildArch=x86
elif [[ $ProcessorArch == x86_64 ]] ; then
	BuildArch=x64
elif [[ $ProcessorArch == arm64 ]] ; then
	BuildArch=arm64
elif [[ $ProcessorArch == aarch64 ]] ; then
	BuildArch=arm64
else
	echo $ProcessorArch is not a recognized architecture
	exit 1
fi

function AbsolutePath()
{
	pushd "$(dirname "$1")" > /dev/null
	printf "%s/%s\n" "$(pwd)" "$(basename "$1")"
	popd > /dev/null
}

MalterlibRoot=`AbsolutePath "../../../.."`
OpenSSLBuildDir="$IntermediateDir/boringssl"

OutputBinDir="$OutputDir/$OutputPlatform/$BuildArch/node"
mkdir -p "$OutputBinDir"

function BuildBoringSSL()
{
	#rm -rf "$OpenSSLBuildDir"
	mkdir -p "$OpenSSLBuildDir"
	pushd "$OpenSSLBuildDir" > /dev/null

	export MACOSX_DEPLOYMENT_TARGET=10.9
	cmake -GNinja "$MalterlibRoot/External/boringssl" -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS="$ExtraBoringSSLFlags" -DCMAKE_C_FLAGS="$ExtraBoringSSLFlags"
	ninja
	#ninja -C "$OpenSSLBuildDir" run_tests

	popd > /dev/null

	mkdir -p "$OpenSSLBuildDir/bin"
	cp "$OpenSSLBuildDir/crypto/libcrypto.a" "$OpenSSLBuildDir/bin"
	cp "$OpenSSLBuildDir/ssl/libssl.a" "$OpenSSLBuildDir/bin"
	cp "$OpenSSLBuildDir/decrepit/libdecrepit.a" "$OpenSSLBuildDir/bin"
}

function BuildNode()
{
	pushd "$MalterlibRoot/External/node" > /dev/null

	./configure --prefix "$IntermediateDir/node_bin" --shared-openssl --shared-openssl-includes "$MalterlibRoot/External/boringssl/include" --shared-openssl-libname crypto,ssl,decrepit --shared-openssl-libpath "$OpenSSLBuildDir/bin"

	make "-j${NumCPUs}"
	make install
	pushd "$IntermediateDir/node_bin/bin" > /dev/null

	export PATH="$PWD:$PATH"
	npm config set python `which python3.9`
	if [[ "$MalterlibNpmSource" != "" ]]; then
		echo "Installing npm from local checkout: $MalterlibNpmSource"
		if [ -d "$MalterlibNpmSource/../cipm" ]; then
			pushd "$MalterlibNpmSource/../cipm" > /dev/null
			npm pack
			CipmPackagePath="`ls $PWD/*.tgz`"
			popd > /dev/null
		fi
		pushd "$MalterlibNpmSource" > /dev/null
		rm -f *.tgz
		if ! [[ -z $(git status -s) ]]; then
			echo $PWD contains changes, please commit before building
			exit 1
		fi
		if [[ "$CipmPackagePath" != "" ]]; then
			mv "$CipmPackagePath" .
			npm install
			npm install "`basename "$CipmPackagePath"`"
		fi
		npm pack
		PackagePath="`ls $PWD/npm-*.tgz`"
		popd > /dev/null
		./npm install -g "$PackagePath"

		pushd "$MalterlibNpmSource" > /dev/null
		git reset --hard
		git clean -fd
		rm -f *.tgz
		popd > /dev/null
	else
		echo "Installing npm from global"
		./npm install -g npm
	fi

	./npm config set python `which python3.9`
	./npm install -g retire
	./npm install -g npm-check-updates

	popd > /dev/null
	pushd "$IntermediateDir"  > /dev/null

	NodePackageName=node-`./node_bin/bin/node --version`-$NodePlatform-$BuildArch

	mv node_bin $NodePackageName
	rm -f "$OutputBinDir"/node-*
	tar -czf "$OutputBinDir/$NodePackageName.tar.gz" $NodePackageName
	RunMD5 "$OutputBinDir/$NodePackageName.tar.gz" > "$OutputBinDir/$NodePackageName.tar.gz.md5"
	echo "Built to $OutputBinDir/$NodePackageName.tar.gz"

	popd > /dev/null
	popd > /dev/null
}

BuildBoringSSL
BuildNode
