#Firmware Makefile
#see example Makefiles at http://www.cs.colby.edu/maxwell/courses/tutorials/maketutor/
#gedit: Ctrl+Shift+U,9,Enter https://askubuntu.com/questions/667418/how-to-enter-a-tab-charater-in-gedit-if-replacement-with-spaces-is-set-up
#make built-in vars: https://www.gnu.org/software/make/manual/html_node/Automatic-Variables.html
#grep multiple patterns: https://superuser.com/questions/537619/grep-for-term-and-exclude-another-term
#debug makefile: --debug=b https://stackoverflow.com/questions/1745939/debugging-gnu-make

DEVICE = 16f1825
TARGET = build/EscapePhone.hex
#https://stackoverflow.com/questions/7507810/how-to-source-a-script-in-a-makefile
include ./scripts/colors.sh

CC = sdcc
CPP = g++ -E #-DINLINE=\#define -DHASH=\#
SHELL = /bin/bash
MPASM = wine "c:\Program Files (x86)\Microchip\MPASM Suite\MPASMWIN.exe"  /ainhx32 /q- /y- /o- #/d__DEBUG=1
MPLINK = wine "C:\Program Files (x86)\Microchip\MPASM Suite\mplink.exe"
EXTRACT_MACROS = grep -e "__SDCC " -e "__SDCC_PIC"
ADD_STARTUP = awk 'END{ print "int main(void);\nvoid _sdcc_gsinit_startup(void)\n{\n\tmain();\n}\n# 1"; }'
REFMT_WARNINGS = sed 's/#pragma message/#warning/g'
#ERRORS_OR_WARNINGS = grep -e "warning" -e "error" | grep -v "unreachable code"
ERRORS_OR_WARNINGS = grep -e "error" | grep -v "unreachable code"
IGNORE_UNREACHABLE = grep -v "unreachable code"
#EXTRACT_ERRORS = awk '/error/{ print "$0"; }'
#EXTRACT_ERRORS = grep "error"
#SHOW_COLORS = sed 's/"\x1b[" "1;33" "m"/
#HASH = \#
#SYNTAX_FIXUP = sed 's/\#pragma message/\#warning/'

#MPASM = wine "~/.wine/drive_c/Program Files (x86)/Microchip/MPASM Suite/MPASMWIN.exe"
#FILE=sdcctest
INCLUDES = -Iincludes  -I/usr/local/bin/../share/sdcc/non-free/include
#INCLUDES = -I./includes
#CFLAGS = -mpic14 $(DEVICE) -DHASH=\# -DINLINE=\#define --debug-xtra --no-xinit-opt --opt-code-speed --fomit-frame-pointer --use-non-free $(INCLUDES)
CFLAGS = -mpic14 -p$(DEVICE) --debug-xtra --no-xinit-opt --opt-code-speed --fomit-frame-pointer --use-non-free
#CFLAGS="-V -mpic14 --debug-xtra --no-xinit-opt --opt-code-speed --fomit-frame-pointer --use-non-free"
#COPTS="-mpic14 --no-xinit-opt --opt-code-speed --fomit-frame-pointer --std-c11 --use-non-free"
#sdcc --version
##sdcc $COPTS $DEVICE $INCL --print-search-dirs
#SOURCE = $(TARGET:.hex=.c)
#sdcc -E sdcctest.c
##sdcc $COPTS $DEVICE $INCL $FILE.c -o build/$FILE
#packihx sdcctest.ihx > sdcctest.hex

#REPORT = grep "pragma message" temp.c

#default target:
all: clean $(TARGET)

clean:
#	rm $(TARGET:/*=)/*.*
	rm -f $(TARGET) $(TARGET:.hex=.asm)
#	ls build

cleaner:
#	rm $(TARGET:/*=)/*.*
	rm -f $(TARGET) $(TARGET:.hex=.*)
#	ls build

macros:
	$(CC) $(CFLAGS) $(INCLUDES) -E -dM ../$(TARGET:.hex=.c)

dirs:
	$(CC) $(CFLAGS) $(INCLUDES) --print-search-dirs

$(TARGET:.hex=.asm): $(@F:.asm=.c)
	@ echo -e "$(BLUE)compile $(@F:.asm=.c) to asm ...$(NORMAL)"
#	$(SYNTAX_FIXUP) < $(@F:.asm=.c) > $(@:.asm=.c)
	- $(CC) -S -V $(CFLAGS) $(INCLUDES) $(@F:.asm=.c) -o $(@:.asm=-ugly.asm) 2>$(@:.asm=.out)
	echo here1
#	- $(CC) -dM -E -V $(CFLAGS) $(INCLUDES) $(@F:.asm=.c) -o $(@:.asm=-macros.asm) #2>$(@:.asm=.out)
# 	- $(CC) -dM -E - < /dev/null
	cat $(@:.asm=.out)
#consolidate messages by type/color:
#explicit colors:
#	@ echo -ne "$(RED)"; cat $(@:.asm=.out) | grep -ie "RED_MSG" | sed s'/RED_MSG\s*//g'; echo -ne "$(NORMAL)"
#	@ echo -ne "$(YELLOW)"; cat $(@:.asm=.out) | grep -ie "YELLOW_MSG"; echo -ne "$(NORMAL)"
#	@ echo -ne "$(GREEN)"; cat $(@:.asm=.out) | grep -ie "GREEN_MSG"; echo -ne "$(NORMAL)"
#	@ echo -ne "$(BLUE)"; cat $(@:.asm=.out) | grep -ie "BLUE_MSG"; echo -ne "$(NORMAL)"
#	@ echo -ne "$(CYAN)"; cat $(@:.asm=.out) | grep -ie "CYAN_MSG"; echo -ne "$(NORMAL)"
#	@ echo -ne "$(MAGENTA)"; cat $(@:.asm=.out) | grep -ie "MAGENTA_MSG" -ie "PINK_MSG"; echo -ne "$(NORMAL)"
#implicit colors:
#	@ echo -ne "$(RED)"; cat $(@:.asm=.out) | grep -iv "[A-Z]_MSG" | grep -ie "error"; echo -ne "$(NORMAL)"
#	@ echo -ne "$(YELLOW)"; cat $(@:.asm=.out) | grep -iv "[A-Z]_MSG" | grep -iv "unreachable code" | grep -iv "overflow" | grep -ie "warning"; echo -ne "$(NORMAL)"
#	@ echo -ne "$(GREEN)"; cat $(@:.asm=.out) | grep -iv "[A-Z]_MSG" | grep -ie "success"; echo -ne "$(NORMAL)"
#	@ echo -ne "$(BLUE)"; cat $(@:.asm=.out) | grep -iv "[A-Z]_MSG" | grep -ie "\[debug\]"; echo -ne "$(NORMAL)"
#	@ echo -ne "$(CYAN)"; cat $(@:.asm=.out) | grep -iv "[A-Z]_MSG" | grep -ie "\[info\]"; echo -ne "$(NORMAL)"
#	@ echo -ne "$(MAGENTA)"; cat $(@:.asm=.out) | grep -iv "[A-Z]_MSG" | grep -iv "error" | grep -iv "warning" | grep -iv "unreachable code" | grep -iv "success" | grep -iv "\[debug\]" | grep -iv "\[info\]"; echo -ne "$(NORMAL)"
	echo here2
#	echo -e "$(cat $(@:.asm=.out) | $(IGNORE_UNREACHABLE) | sed 's/error/$(RED)/g')"
##	cat $(@:.asm=.out) | grep -v "unreachable code"
#echo -e "$(sed -e 's/OK/\\033[0;32mOK\\033[0m/g' test_results.txt)"
#apply asm fixups then run thru mpasm:
	@ echo -e "$(BLUE)asm fixup ...$(NORMAL)"
	scripts/asm-fixup-SDCC.js < $(@:.asm=-ugly.asm) > $(@:.asm=-fixup.asm)
#make a cleaner version for debug/readability:
	@ echo -e "$(BLUE)asm clean-up...$(NORMAL)"
	cat $(@:.asm=-fixup.asm) | grep -v "^\s*;" | sed 's/;;.*$$//' >$(@)

too_complicated:
#	echo src $(@F:.hex=.c)
	@ echo -e "$(BLUE)translating to c...$(NORMAL)"
#	source ../scripts/colors.sh && echo -e "$(CYAN) hello $(NORMAL)"
#kludge: run thru sdcc first to extraxct compiler + device macros
#NOTE: $< (ie, dependency) doesn't seem to work here?
	@ $(CC) $(CFLAGS) $(INCLUDES) -E -dM $(@F:.asm=.c) 2> /dev/null | grep -e "__SDCC " -e "__SDCC_PIC" | awk '{ print $0; }END{ print "int main(void);\nvoid _sdcc_gsinit_startup(void)\n{\n\tmain();\n}\n# 1"; }' | cat - $(@F:.asm=.c) >$(@:.asm=-prep1.c)
#	@ $(CC) $(CFLAGS) $(INCLUDES) -E -dM $(@F:.asm=.c) 2> /dev/null | $(EXTRACT_MACROS) | $(ADD_STARTUP) | cat - $(@F:.asm=.c) >$(@:.asm=-prep1.c)
#	$(CPP) $(INCLUDES) source.c | sed 's/#pragma message/#warning/g' | tee temp.c | grep "#warning" #|| exit 1
#kludge: use gcc preproc to handle expr within messages and macros better, if errors show messages:
	@ ($(CPP) $(INCLUDES) $(@:.asm=-prep1.c) | sed 's/#pragma message/#warning/g' > $(@:.asm=-prep2.c)) || (echo "#warning only if errors" >>$(@:.asm=-prep2.c); grep -e "#warning" -e "#error" < $(@:.asm=-prep2.c)) #|| exit 1
#	@ ($(CPP) $(INCLUDES) $(@:.asm=-prep1.c) | $(REFMT_WARNINGS) > $(@:.asm=-prep2.c)) || (echo "#warning only if errors" >>$(@:.asm=-prep2.c); $(ERRORS_OR_WARNINGS) < $(@:.asm=-prep2.c)) #|| exit 1
#	@ echo "-----------------------------------"
#now do the real compile:
	@ echo -e "$(BLUE)compiling to asm...$(NORMAL)"
	$(CC) -S -V $(CFLAGS) $(INCLUDES) $(@:.asm=-prep2.c) -o $(@:.asm=-ugly.asm) | $(ERRORS_OR_WARNINGS)
#apply asm fixups then run thru mpasm:
	@ echo -e "$(BLUE)fixing up asm...$(NORMAL)"
	scripts/asm-fixup-SDCC.js <$(@:.asm=-ugly.asm) >$(@)
	@ echo -e "$(BLUE)cleaning up asm...$(NORMAL)"
	cat $(@) | grep -v "^\s*;" >$(@:.asm=-clean.asm)

$(TARGET): $(TARGET:.hex=.asm)
	@ echo -e "$(BLUE)assembling to hex...$(NORMAL)"
#	$(MPASM) /p$(DEVICE) $< #/l$(@:.hex=.lst) /e$(@:.hex=.err)
	$(MPASM) /p$(DEVICE) $<
#	$(MPLINK)  /p$(DEVICE) $(@:.hex=.o) /z__MPLAB_BUILD=1 /o$(@:.hex=.cof) /M$(@:.hex=.map) /W /x #/u_DEBUG /z__MPLAB_DEBUG=1

#EOF