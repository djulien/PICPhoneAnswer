#!/bin/echo ERROR - Please run this script as follows: source
#from https://stackoverflow.com/questions/3664225/determining-whether-shell-script-was-executed-sourcing-it

#from http://stackoverflow.com/questions/5947742/how-to-change-the-output-color-of-echo-in-linux
#NOTE: node-gyp requires single quotes, no spaces around "=" below
RED='\e[1;31m' #too dark: '\e[0;31m' #`tput setaf 1`
GREEN='\e[1;32m' #`tput setaf 2`
YELLOW='\e[1;33m' #`tput setaf 3`
BLUE='\e[1;34m' #`tput setaf 4`
PINK='\e[1;35m' #`tput setaf 5`
CYAN='\e[1;36m' #`tput setaf 6`
GRAY='\e[0;37m'
ENDCOLOR='\e[0m' #`tput sgr0`

#eof#
