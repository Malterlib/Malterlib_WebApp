#!/bin/bash

set -e

if [[ "$NpmInstallInContainer" == "true" ]]; then
	unset NpmInstallInContainer

	SetupVersion=1
	NodeVersion=22.21.0

	if ! [ -f ~/setup.version ] || [[ `cat ~/setup.version` != "$SetupVersion" ]]; then
		apt-get update -y
		apt-get dist-upgrade -y
		apt-get install -y software-properties-common
		add-apt-repository -y ppa:deadsnakes/ppa
		apt-get update -y
		apt-get install -y bsdmainutils file build-essential htop psmisc zstd libarchive-tools wget
		wget https://nodejs.org/dist/v${NodeVersion}/node-v${NodeVersion}-linux-${Architecture}.tar.xz

		tar -C /usr/local --strip-components=1 -xJf node-v${NodeVersion}-linux-${Architecture}.tar.xz

		if ! [[ -d "/home/builduser" ]]; then
			useradd -s "/bin/bash" -d "/home/builduser" -m "builduser"
		fi

		echo "$SetupVersion" > ~/setup.version
	fi

	chown -R builduser /opt/work/*
	su builduser "$0" "$@"
	exit
fi

ScriptDir="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

export PATH="/usr/local/bin:$PATH"

function KillAfterTimeout()
{
	SECONDS=0
	while true; do
		if [[ $SECONDS > 600 ]]; then
			echo "error: Timed out installing npm."
			killall -SIGKILL node || true
			exit
		fi

		killall -SIGSTOP node || true
		killall -SIGCONT node || true

		sleep 10
	done
}

function CleanupExit()
{
	if [[ "$KillAfterTimeoutPid" != "" ]]; then
		kill $KillAfterTimeoutPid || true
	fi
}

trap CleanupExit EXIT

export NPM_CONFIG_PROGRESS=false

KillAfterTimeout &
KillAfterTimeoutPid=$!

cd /opt/work/App
npm ci --production

kill $KillAfterTimeoutPid || true
KillAfterTimeoutPid=""

exit 0
