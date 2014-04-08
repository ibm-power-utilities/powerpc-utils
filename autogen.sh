#!/bin/sh

set -e

if [ ! -d config ];
then
	mkdir config;
fi

autoreconf --install --verbose --force
