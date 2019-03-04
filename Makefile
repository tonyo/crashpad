SHELL := /bin/bash
PATH := $(PWD)/../depot_tools:$(PATH)

all:
	echo 'Nothing to do' && exit 1

build:
	gn gen out/Default
	ninja -C out/Default
.PHONY: build

update:
	gclient sync
