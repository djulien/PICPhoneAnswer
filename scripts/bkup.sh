#!/bin/bash
tar -cvf ../ssr-ws281x.tar --exclude=node_modules --exclude=tools * --exclude=build
#tar -cvf ../ssr-ws281x.tar --exclude=node_modules * --exclude=build
