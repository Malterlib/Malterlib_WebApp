#!/usr/bin/env bash

set -e

OutputDir="$1"
IntermediateDir="$2"

if [[ "$OutputDir" == "" ]]; then
	echo "No output dir specified"
	exit 1
fi

if [[ "$IntermediateDir" == "" ]]; then
	IntermediateDir="/CompiledFiles/BuildNode"
	rm -rf "$IntermediateDir"
fi

SysName=$(uname -s)
ProcessorArch=$(uname -m)

if [[ $SysName ==  Darwin* ]] ; then
	NodePlatform=darwin
	OutputPlatform=OSX
	NumCPUs=`getconf _NPROCESSORS_ONLN`
	BuildPlatform=OSX10.7
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
	BuildPlatform=Linux2.6
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

OutputBinDir="$OutputDir/$OutputPlatform/node"
mkdir -p "$OutputBinDir"

function BuildBoringSSL()
{
	#rm -rf "$OpenSSLBuildDir"
	mkdir -p "$OpenSSLBuildDir"
	pushd "$OpenSSLBuildDir" > /dev/null

	export MACOSX_DEPLOYMENT_TARGET=10.7
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
	if [[ "$MalterlibNpmSource" != "" ]]; then
		echo "Installing npm from local checkout: $MalterlibNpmSource"
		if [ -d "$MalterlibNpmSource/../cipm" ]; then
			pushd "$MalterlibNpmSource/../cipm" > /dev/null
			rm -f *.tgz
			npm pack
			CipmPackagePath="`ls $PWD/*.tgz`"
			popd > /dev/null
		fi
		pushd "$MalterlibNpmSource" > /dev/null
		if [[ "$CipmPackagePath" != "" ]]; then
			rm -rf node_modules package-lock.json
			cp "$CipmPackagePath" .
			npm install
			npm install "`basename "$CipmPackagePath"`"
		fi
		rm -f *.tgz
		npm pack
		PackagePath="`ls $PWD/*.tgz`"
		popd > /dev/null
		./npm install -g "$PackagePath"
	else
		echo "Installing npm from global"
		./npm install -g npm
	fi

	popd > /dev/null
	pushd "$IntermediateDir"  > /dev/null

	NodePackageName=node-`./node_bin/bin/node --version`-$NodePlatform-$BuildArch

	mv node_bin $NodePackageName
	tar -czf "$OutputBinDir/$NodePackageName.tar.gz" $NodePackageName
	RunMD5 "$OutputBinDir/$NodePackageName.tar.gz" > "$OutputBinDir/$NodePackageName.tar.gz.md5"
	echo "Built to $OutputBinDir/$NodePackageName.tar.gz"

	popd > /dev/null
	popd > /dev/null
}

BuildBoringSSL
BuildNode
