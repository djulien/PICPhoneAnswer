#!/usr/bin/env node
//https://sites.google.com/site/lccretargetablecompiler/
//https://github.com/drh/lcc
//http://www.homebrewcpu.com/retargeting_lcc.htm
//http://www.compilers.de/vbcc.html
//http://www.uclinux.org/pub/uClinux
//* http://fpgacpu.org/papers/xsoc-series-drafts.pdf
//http://fpgacpu.org/usenet/lcc.html
//* http://fd.fabiensanglard.net/quake3/building_a_c_based_processor.pdf
//http://scottmcpeak.com/elkhound/sources/elsa/
//http://ctool.sourceforge.net/
//https://people.eecs.berkeley.edu/~necula/cil/

//ASM fixup script
//This allows more efficient code generation than the off-the-shelf compiler from Microchip,
// in some cases down to instruction cycle precision.  This is needed by timing-critical logic
// such as WS281X bit-banging written in C for low-end 8-bit ucontrollers.
//Copyright (c) 2013-2017 Don Julien
//Can be used for non-commercial purposes
//
//History:
// 3/4/13 DJ added SWOPCODE for WS2811 on PIC16F1827
// 3/24/13 DJ added MOVIW, MOVWI
// 4/12/13 DJ added ABSDIFF for 8-bit jump offset mgmt
// 4/25/13 DJ added ATADRS to allow more precise placement of code
// 5/4/13  DJ added LEZ
// 3/20/14 DJ added indf1_auto* for parallel ws2811
// 5/20/17 DJ rewrote in node.js; reworked tool chain (replaced proprietary/licensed SourceBoost with free Microchip HTC/PICC-Lite/XC8 compiler)

'use strict'; //find bugs easier
require('colors');
const fs = require('fs');
//const readline = require('readline');
//const readline = require('line-reader');
//const readline = require('linebyline');
//const fse = require('fs-extra');
const chproc = require("child_process");
const RegEsc = require('escape-string-regexp');
//const exec = require('sync-exec');
//const filewatcher = require('filewatcher');

//const CC = "/usr/bin/wine ~/.wine/drive_c/\"Program Files (x86)\"/Microchip/xc8/v1.42/bin/xc8.exe"; //.replace(/~/, "/home/dj");
const CC = "~/.wine/drive_c/\"Program Files (x86)\"/Microchip/xc8/v1.42/bin/xc8.exe".replace(/~/, "/home/dj"); //.replace(/Program Files \(x86\)/, "ProgFiles-x86");
//command line args from MPLAB:
//process.argv.forEach((arg, inx, all) => { console.log("arg[%d/%d]: '%s'", inx, all.length, arg); });
const ProjectName = process.argv[2] || "No project?";
const TargetName = process.argv[3] || "No target?";
const OutputDir = (process.argv[4] || "No output folder?").replace(/\\/g, "/");
const device = (process.argv[5] || "No device?").replace(/^PIC/, "");
const phase = process.argv[6] || "No compile phase?";
const basefile = TargetName.replace(/\..*$/, "");
const verbose = (process.argv[7] == "-v");
console.log(`ASM fixup for device '${device}', file '${basefile}.c', phase '${phase}', verbose? ${verbose}`.blue_lt);

/*
var watcher = filewatcher();
watcher.add(__filename);
watcher.on('change', function(file, stat) {
  console.log('File modified: %s', file);
  if (!stat) console.log('deleted');
});
*/

//for (var i=0; i < 10; ++i)
//    console.error("msg %d", i);
//process.stdin.pause(); //end();
//process.stdin.unref();
//process.stdout.end();
//process.stderr.end();
//process.exit(1);

///////////////////////////////////////////////////////////////////////////////
////
/// Main logic
//

function main()
{
    switch (phase)
    {
        case "pre": pre(); break;
        case "post": post(); break;
        case "devices": devices(); break;
        default: throw `Unknown phase: '${phase}'`;
    }
    return 0;
}


//apply fixups before running MPASM:
function pre()
{
//    console.log("pre phase");
/*
    var fd = fs.createWriteStream("djtest.asm"); //, {flags: 'a'});
    fd.on('finish', function () { console.log('file has been written'); }); //no worky
    fd.write(`
	title "djtest generated ${Date.now()}"
	LIST
	include "P12F1840.inc"

	ORG 0
main
	GOTO main

	END`);
    console.log("write");
    fd.end(function() { console.log("ended"); }); //no worky
*/
    if (!compile()) return;
    if (!scan()) return;
    if (!fixup()) return;
}


//apply fixups after running MPASM:
function post()
{
    console.log("post phase");
//    console.log("running " + cmdline);
//    var stdout = chproc.execSync(cmdline); //, opts);
//    console.log("ran", stdout.toString());
}


//show supported devices:
function devices()
{
//NOTE: chipinfo comes from "C:\Program Files (x86)\Microchip\xc8\v1.42\dat\picc.ini"
    var cmdline = `${CC} --chipinfo`;
//    chproc.exec(cmdline).unref();
//    console.log("running " + cmdline);
    var stdout = chproc.execSync(cmdline).toString().split(/[\r\n]+/); //, opts);
//    console.log("ran", stdout.toString().replace(/[\r\n]/g, " "));
//    if (device != "12F1840") throw `Unknown device: '${device}'`;
    stdout.forEach(dev =>
    {
        if (dev.match(/^(12F18\d\d|16F(688|182\d))/)) console.log(dev); //these are the only ones of interest currently
    });
}


///////////////////////////////////////////////////////////////////////////////
////
/// First phase: generate assembly code
//

//run c source thru compiler, generate assembler output:
function compile()
{
//    const cmdline = "%CC%  %OPTS%  %FILES%";
//    console.log(CC.replace(/^\/usr\/bin\/wine (.*)\/bin\/xc8\.exe/, "$1/include").cyan_lt);
//    const srcfile = `./temp/${basefile}-prepped.c`;
//    const errfile = `./output/${basefile}#PASS#.errs`; //__dirname?
//    const lstfile = `./output/${basefile}-prepped.lst`; //__dirname?
//    try { fs.truncateSync(mydir(errfile), 0); } catch(exc) {}; //ignore errors if file not there
//    try { fs.truncateSync(lstfile, 0); } catch(exc) {}; //ignore errors if file not there
//    const cmdlines =
//    [
//        `ls
//            ${CC.replace(/^\/usr\/bin\/wine (.*)\/bin\/xc8\.exe/, "$1/include")}`,
//for gcc see https://gcc.gnu.org/onlinedocs/gcc-2.95.2/gcc_2.html
/*
        `gcc
            -D__CCI__
            -D__XC8__
            -D_${device}
            -Wp,-v
            -E
            -I${CC.replace(/\/bin\/xc8\.exe$/, "/include")}
            -I./includes
            ${basefile}.c > ${srcfile}`,
*/
//for XC8 see chapter 2.5 and 4 in http://ww1.microchip.com/downloads/en/DeviceDoc/50002053G.pdf
//NOTE: --chip= seems must be first in order to avoid error 902
//noworky; messes up following params:            -DSEMI=;
//use this only if unoptimized code is too big:            --maxipic
    const cmdline =
        `/usr/bin/wine ${CC}
            --chip=${device}
            -DCPP_PREFIX=#
            -E./output/${basefile}#PASS#.errs
            -G
            -I./includes
            -M
            -S
            -V
            --addrqual=require
            --asmlist
            --codeoffset=4
            --debugger=pickit2
            --ext=cci
            --fill=0xDEAD
            --html
            --msgdisable=913:off,1273:off,1487:off
            --mode=free
            --nodel
            --opt=all,-asm
            --objdir=./temp
            --outdir=./output
            --output=inhx32
            --pre
            --proto
            --runtime
            --strict
            --summary
            --time
            #SRCFILE#`;
//        `mv ./output/${basefile}.pre ./output/${basefile}.c`,
//${basefile}.c`;
//            ${srcfile}`,
//        `cat ./output/${basefile}.errs`,
//            -E${errfile}
//            --asmlist
//            --ver
//        '#PASS2#',
//    ];
//    --echo
//    --memmap=${basefile}.map
//-V
//-S
    const srcfiles = [`./temp/${basefile}.pre`, `./${basefile}.c`, `./temp/${basefile}.c`];
    var asmfile = mydir(`./output/${basefile}.as`);

//    console.log("running " + cmdline);
//    cmdline = "ls -l " + CC.replace(/wine /, "");
//    cmdline += "; mv " + asmout + " " + asmout + "m"; //MPASM wants .asm file
//    cmdlines.forEach((cmd, inx) =>
//    for (var inx = 0; inx < cmdlines.length; ++inx)
    var errs = [];
    for (var pass = 1; pass <= 2; ++pass)
    {
        var cmd = cmdline; //s[inx];
        cmd = cmd.replace(/\n\s*/g, "  "); //.replace(/^\s*/, ""); //strip newlines
        cmd = cmd.replace(/#SRCFILE#/g, srcfiles[pass]).replace(/#PASS#/g, pass);
//console.log(cmd);
        var errfile = mydir("./" + cmd.replace(/^.*\s-E([^ ]+)\s.*$/m, "$1"));
        if (verbose) console.error("errfile", errfile);
//        errfile = "";
//        try { fs.truncateSync(errfile, 0); } catch(exc) {}; //ignore errors if file not there
//        try { fs.writeFileSync(errfile, " "); } catch(exc) {}; //ignore errors if file not there
        if (pass > 1)
        {
            fs.rename(mydir(srcfiles[0]), mydir(srcfiles[2]));
            cmd = cmd.replace(/ --pre /, "");
        }
//        else cmd = cmd.replace(/ --time /, "");
        try
        {
            if (verbose) console.log("CMD[%d]", pass, cmd);
            var stdout = chproc.execSync(cmd, // + " 2>&1",
            {
                cwd: __dirname,
                env: process.env,
            });
//    chproc.exec(cmdline, (error, stdout, stderr) =>
//    {
//        console.log('stdout: ' + stdout);
//        console.log('stderr: ' + stderr);
//        if (error) console.log('exec error: ' + error);
//    });
//        if (inx) //fs.writeFileSync(__dirname + "temp/file, data[, options])
//            console.log("ran", stdout.toString());
        }
        catch (exc)
        {
//            console.log("\nexc[%d]".red_lt, pass, exc.toString());
            errs.push(`exc[${pass}]: ` + exc);
            break;
        }
        finally
        {
            var ignored = 0, warnings = 0;
            var more_errs = errfile? fs.readFileSync(errfile).toString().split("\n"): [];
            if (!verbose) //filter out uninteresting msgs
                more_errs.forEach((errline, inx, all) =>
                {
                    if (!errline) { ++ignored; return; }
                    if (errline.indexOf(".exe @C") != -1) { ++ignored; return; }
                    if (errline.indexOf("floating-point libraries") != -1) { ++ignored; return; } //++warnings;
                    if (errline.indexOf("#warning:") != -1) ++warnings;
                    if (errline.indexOf("Compilation stage times:") != -1) warnings += [0, 3, 6][pass];
                    if (ignored) all[inx - ignored] = all[inx]; //compact array
                });
//console.error("errs ignored: %d, kept %d", ignored, errs.length - ignored)
            if (ignored) more_errs = more_errs.slice(0, -ignored);
//            errs = errs.join("\n");
            if (errfile) console.log("Pass %d: %d ERRORS (%d ignored), %d warnings:\n".red_lt, pass, more_errs.length - warnings, ignored, warnings, more_errs.join("\n")); //.toString());
//console.log("#errs %d, #warns %d", errs.length, warnings);
            if (more_errs.length > warnings) return false;
//                else
//                    console.log("GOOD");
        }
//        catch (exc) { console.error("EXC2", exc.toString()); };

//    fs.rename(asmout, asmout + "m", err => //MPASM wants .asm file
//    {
//        if (err) console.log('ERROR: ' + err);
//        else console.log("renamed okay");
//    });
//    fse.moveSync(asmout, asmout + "m");
    }//);
    return true;
}


///////////////////////////////////////////////////////////////////////////////
////
/// Second phase: examine asm output from C compiler
//

function scan()
{
    var asmfile = __dirname + `/output/${basefile}.as`;
//debugger;

//named psects will point to classes, which define address space:
    scan.segs = 
    {
        CODE: {class: "CODE", start: 0, limit: 0x800}, //use explict code pages instead
        STRCODE: {class: "CODE", start: 0, limit: 0x800}, //use explict code pages instead
//TODO        PAGE0: {class: "CODE", start: 0, limit: 0x800},
//TODO        PAGE1: {class: "CODE", start: 0x800, limit: 0x1000},
        COMMON: {class: "COMMON", start: 0x70, limit: 0x10}, //shared (non-banked) memory //name: "bssCOMMON", inx: 0},
        BANK0: {class: "BANK0", start: 0x20, limit: 0x50}, //name: "bssBANK0", inx: 1},
        BANK1: {class: "BANK1", start: 0xA0, limit: 0x50}, //name: "bssBANK1", inx: 2},
        BANK2: {class: "BANK2", start: 0x120, limit: 0x50}, //name: "bssBANK2", inx: 3},
//        BANK3: {class: "BANK3", start: 0x1A0, limit: 0x50}, //name: "bssBANK3", inx: 4},
        LINEAR: {class: "LINEAR", start: 0x2000, limit: 3 * 0x50},
    };
//    scan.segs.forEach(seg => { seg.items = []; });

//    scan.equs = {};
//    scan.consts = {};
    scan.fncalls = {};
//    scan.globals = {};
    scan.symbols = {};
    scan.linenum = 0;
    scan.allocque = []; //allocate data in order of occurrence
    readline.eachLine(asmfile, (inbuf, islast) =>
    {
        ++scan.linenum;
//        var isprog = scan.curseg && (scan.curseg.class.indexOf("CODE") != -1); //code vs. data
        var remainder = inbuf.replace(/\s*(;.*)?$/, " "), parts; //remove comment; add trailing space for simpler parsing
        if (!remainder || remainder.match(/^\s*$/)) return; //discard empty/blank line

//directives:
        if (remainder.match(/^\s+(signat|processor|opt|file|line)\s/i)) return; //discard
        if (parts = remainder.match(/^\s+global\s+([a-z0-9@?_]+)\s+$/i))
        {
            var [, name] = parts;
//NOTE: globals seem to be functions or volatile vars
//            var name = scan.symbols[parts[1]] || (scan.symbols[parts[1]] = {}); //fwd ref: symbol needs to be defined later
            symb_def(name, true);
//            if (scan.symbols.hasOwnProperty(name)) throw `duplicate definition for '${name}' on line ${scan.linenum}`;
//            scan.symbols[name] = {name, linenum: scan.linenum};
            return; //continue
        }
        if (parts = remainder.match(/^\s+psect\s+([a-z0-9@?_]+)(,.*)?\s+$/i))
        {
            var [, name, attrs] = parts;
            scan.curseg = scan.segs[name];
            if (!attrs && !scan.curseg) return symberr("unknown psect", name);
            if (attrs && scan.curseg) return symberr("duplicate psect def", name);
            if (!scan.curseg) scan.curseg = scan.segs[name] = {name, linenum: scan.linenum};
            if (attrs)
                attrs.split(/\s*,\s*/).forEach(param =>
                {
                    var [lhs, rhs] = param.split(/\s*=\s*/);
                    scan.curseg[lhs] = rhs || true;
                });
//            if (!scan.curseg.global) throw `non-global psects not implemented '${scan.curseg.name}' at line ${scan.linenum}`;
            if (!scan.segs[scan.curseg.class]) return symberr("unknown psect class", scan.curseg.class);
            scan.curseg.latest_linenum = scan.linenum;
            scan.curseg.items = []; //NOTE: in some cases .length is used as a scalar without array elements
            set_wreg();
            return; //continue
        }
        if (parts = remainder.match(/^([a-z0-9@?_]+):\s+$/i)) //assume labels are on a separate line by themselves
        {
//            var where;
            var [, name] = parts;
            if (!scan.symbols[name]) return; //just ignore undeclared vars (they are probably temps) ;// throw `unknown name '${parts[1]}' on line ${scan.linenum}`;
//            else if (scan.symbols[name].hasOwnProperty('adrs')) throw `duplicate definition for '${name}' on line ${scan.linenum}`;
//            if (isprog) //enforce page designators
//            if (where = parts[1].match(/bank(\d)/i)) //compiler doesn't honor bank designators; enforce it here
//            {
//            }
            Object.assign(symb_def(name, false), {psect: scan.curseg.name, value: scan.curseg.items.length, how: "label", linenum: scan.linenum});
            if (!IsProg()) scan.allocque.push(scan.symbols[name]);
            scan.curseg.page = name; //PCLATH had to have been correct to get to here (assuming no page spanning)
            return; //continue
        }
        if (parts = remainder.match(/^\s+ds\s+(\d+)\s+$/i))
        {
            var [, size] = parts;
            if (IsProg()) return symberr("Found data in code seg");
            scan.curseg.items.length += 1 * size; //NOTE: changes len but does not create array elements
            return; //continue
        }
        if (parts = remainder.match(/^([a-z0-9@?_]+)\s+equ\s+([a-z0-9@?_+&|^() -]+)\s+$/i))
        {
            var [, name, expr] = parts;
//NOTE: expr must be evaluated during MPASM second pass
            Object.assign(symb_def(name, false), {value: expr, isexpr: expr.match(/[+\-&|^]/), how: "equ", linenum: scan.linenum});
            return; //continue
        }
        if (parts = remainder.match(/^([a-z0-9@?_]+)\s+set\s+(\d+)\s+$/i))
        {
            var [, name, expr] = parts;
//            if (isnum(expr)) throw `Symbol '${name}' at line ${scan.lines.length} already has a value from line ${dup.line}`;
//            if (scan.consts.hasOwnProperty(name)) throw `duplicate definition for '${name}' on line ${scan.linenum}`;
//            scan.consts[name] = {name, value: 1 * expr, linenum: scan.linenum};
            Object.assign(symb_def(name, false), {value: 1 * expr, how: "set", linenum: scan.linenum});
            return; //continue
        }
        if (parts = remainder.match(/^\s+dabs\s+(\d+)\s*,\s*(0x[0-9a-f]+)\s*,\s*(\d+)(\s*,\s*([a-z0-9@?_]+))?\s+$/i))
        {
            var [, memspace, addr, size,, name] = parts;
            if (!name) return; //ignore if unnamed; device has fixed memory anyway
//equivalent to global + equ? global seems to be there already
//            if (scan.globals.hasOwnProperty(name) || scan.equs.hasOwnProperty(name)) throw `duplicate definition for '${name}' on line ${scan.linenum}`;
//            if (!scan.symbols.hasOwnProperty(name)) scan.symbols[name] = {name, linenum: scan.linenum};
//            else if (scan.symbols[name].hasOwnProperty('value')) throw `duplicate definition for '${name}' on line ${scan.linenum}`;
//            scan.equs[name] = {name, value: parseInt(addr.substr(2), 16), linenum: scan.linenum};
//            scan.globals[name] = {name, size, linenum: scan.linenum};
            Object.assign(symb_def(name, false), {value: parseInt(addr.substr(2), 16), size, how: "dabs", linenum: scan.linenum});
            scan.allocque.push(scan.symbols[name]);
            return; //continue
        }
        if (parts = remainder.match(/^\s+FNROOT\s+([a-z0-9@?_]+)\s+$/i))
        {
            var [, name] = parts;
            if (scan.fnroot) return symberr("duplicate fnroot");
            scan.fnroot = {name, linenum: scan.linenum};
            return; //continue
        }
        if (parts = remainder.match(/^\s+FNCALL\s+([a-z0-9@?_]+)\s*,\s*([a-z0-9@?_]+)\s+$/i))
        {
            var [, src, dest] = parts;
            if (scan.fncalls[src + ":" + dest]) return symberr("duplicate fncall", `'${src}' -> '${dest}'`);
            scan.fncalls[src + ":" + dest] = {src, dest, linenum: scan.linenum};
            return; //continue
        }
//TODO:
//ORG ##h
//DB #,#,#
//DW
        if (!IsProg()) return symberr("Found opcode in data seg");

//context/flow control opcodes:
//TODO: conditional?
//TODO: move movl* back to previous label but not prev select
        const Targets = {'movlb': "bank", 'movlp': "page", 'movlw': "wreg", 'addlw': "wreg", 'retlw': "wreg"};
//        if (parts = remainder.match(/^\s+(movlb|movlp|movlw|addlw|retlw)\s+(\d+)\s+$/i)) //might be able to rearrange these opcodes
        if (parts = remainder.match(/^\s+(movlb|movlp)\s+(\d+)\s+$/i)) //might be able to rearrange these opcodes
        {
            var [, opc, which] = parts;
            scan.curseg[Targets[opc]] = which; //don't emit until we know we need it
            return; //continue
        }
        if (parts = remainder.match(/^\s+(goto|call|ljmp|fcall)\s+([a-z0-9@?_]+)\s+$/i))
        {
            var [, opc, dest] = parts;
            var svpage = scan.curseg.page;
//            if (opc == "goto" || opc == "call")
            scan.curseg.page = dest; //might need a movlp
            emit(remainder);
//TODO: unchanged state if jump falls thru (must have been conditional false)
            if (opc.indexOf("call") != -1) //unknown state after return from function
            {
                scan.curseg.bank = null;
                scan.curseg.page = svpage; //PCLATH is restored during return from function
                set_wreg(); //unknown upon return from function
            }
            return; //continue
        }
        if (remainder.match(/^\s+(return)\s+$/i))
        {
            scan.curseg.bank = scan.curseg.page = null; //restored during return; unknown if fall-thru
            emit(remainder);
            return; //continue
        }

//arithmetic/data opcodes:
        if (parts = remainder.match(/^\s+(movlw|addlw|sublw|andlw|iorlw|xorlw|retlw)\s+([a-z0-9@?_+&|^() ]+)\s+$/i)) //might be able to rearrange these opcodes; NOTE: can be a compile-time expression
        {
            var [, opc, expr] = parts;
//            if (scan.curseg.conditional) throw `Conditional banksel at line ${scan.linenum}`;
            if ((opc == "movlw") && (expr == scan.curseg.wreg)) return; //skip redundant value
            set_wreg((opc == "movlw")? expr: null);
//            if (parts[1] == "addlw") newval += scan.curseg.wreg;
            emit(remainder); //just issue opcode rather than tracking wreg contents
            return; //continue
        }
        if (parts = remainder.match(/^\s+(movwf)\s+([a-z0-9@?_+&|^() ]+)\s+$/i))
        {
            var [, opc, expr] = parts;
            var where = scan.curseg.bank + ":" + expr; //TODO: look up adrs
            scan.curseg.wreg_copies[where] = scan.linenum; //remember where wreg is stored
            if (isnum(expr) || scan.symbols.hasOwnProperty(expr)) emit(remainder); //set user var as requested
            return; //continue
        }
        if (parts = remainder.match(/^\s+(movf)\s+([a-z0-9@?_+&|^() ]+)\s*,\s*([fw])\s+$/i)) //NOTE: allow address arithmetic here
        {
            var [, opc, expr, dest] = parts;
            if (dest == "f") { emit(remainder); return; } //must preserve opcode when checking C, Z, etc
            var where = scan.curseg.bank + ":" + expr; //TODO: look up adrs
            if (scan.curseg.wreg_copies[where]) return; //it's already there
            set_wreg(where);
            return; //continue
        }
        if (remainder.match(/^\s+(clrf)\s+([a-z0-9@?_+&|^() ]+)\s+$/i)) //NOTE: allow address arithmetic here
        {
            emit(remainder); //set user var as requested
            return; //continue
        }
        if (parts = remainder.match(/^\s+(andwf|addwf|addwfc|subwf)\s+([a-z0-9@?_+&|^() ]+)\s*,\s*([fw])\s+$/i))
        {
            var [, opc, expr, dest] = parts;
            if (dest == "w") set_wreg();
            emit(remainder); //set user var as requested
            return; //continue
        }

//bit opcodes:
        if (remainder.match(/^\s+(bcf|bsf)\s+(\d+)\s*,\s*(\d+)\s+$/i))
        {
            emit(remainder);
            return; //continue
        }
        if (remainder.match(/^\s+(btfsc|btfss)\s+(\d+)\s*,\s*(\d+)\s+$/i))
        {
            emit(remainder);
            return; //continue
        }

        symberr("unknown opcode", remainder);
    });
    return true;
}


//create or get a symbol def:
function symb_def(name, want_create)
{
    if (want_create === true)
    {
        if (scan.symbols.hasOwnProperty(name)) return symberr("duplicate definition", name);
        scan.symbols[name] = {name, linenum: scan.linenum};
    }
    if (want_create === false)
    {
        if (!scan.symbols.hasOwnProperty(name)) return symberr("undefined", name);
    }
    if (scan.symbols[name].hasOwnProperty('value')) return symberr("duplicate definition", name);
    return scan.symbols[name];
}


//keep track of wreg contents:
//this allows loads generated by XC compiler to be removed
//used mainly for constant values; could be used with evaluated expressions but that's more complicated
function set_wreg(val)
{
    scan.curseg.wreg = val;
    scan.curseg.wreg_copies = {}; //new value not stored anywhere
}


//add an opcode to psect and save state/context:
function emit(opc)
{
    opc = opc.replace(/\t/g, " "); //makes debug easier
    var newopc = {opc, wreg: scan.curseg.wreg, bank: scan.curseg.bank, page: scan.curseg.page, wreg_copies: Object.keys(scan.curseg.wreg_copies || {}).join("\n"), linenum: scan.linenum};
//    scan.segs[scan.curseg.class].items.push(newopc);
    scan.curseg.items.push(newopc);
}


//synchronous line reader:
const readline =
{
    eachLine: function(filename, cb)
    {
        fs.readFileSync(filename).toString().split(/\r?\n/).forEach((line, inx, all) =>
        {
            cb(line, inx == all.length - 1);
        });
    },
};


function IsProg(seg)
{
    return (((seg || scan.curseg || {}).class || "").indexOf("CODE") != -1); //code vs. data
}


function symberr(desc, name)
{
    throw `${desc} '${name}' at line ${scan.linenum}`;
}


///////////////////////////////////////////////////////////////////////////////
////
/// Third phase: optimize and apply fixups
//

//fix up assembler output before passing to MPASM:
function fixup()
{
//TODO: fixup in memory, then dump/emit
//TODO: chained gotos/calls/returns

//    console.error("scan results:".cyan_lt);
//    for (var i in scan)
//        if (scan.hasOwnProperty(i))
//            console.error("scan[%s]: %j".cyan_lt, i, scan[i]);

    if (false)
    [
//        "segs",
        "symbols",
//        "fnroot",
//        "fncalls",
    ].forEach(name =>
    {
        for (var i in scan[name])
        {
            var info = (name == "seqs")? ` ${scan.segs[i].items.length} items`: "";
            console.error("%s[%s]%s:\n".cyan_lt, name, i, info, scan[name][i]);
        }
    });

//check for undefined symbols:
    if (!scan.symbols.start.hasOwnProperty('value')) Object.assign(scan.symbols.start, {value: 0, how: "set"}); //kludge: add missing reset vector

    var undef = [];
    for (var name in scan.symbols)
    {
        var symbol = scan.symbols[name];
        if (!symbol.hasOwnProperty('value')) undef.push(`${name} (line ${scan.symbols[name].linenum})`);
        if (!symbol.hasOwnProperty('how')) symberr("Don't know how to allocate", name);
    }
    if (undef.length) throw "Symbols with no value: " + undef.join(", ");

//assign final memory addresses to data:
//NOTE: xc8 compiler doesn't honor bank requests (even with --addrqual), so assign them using custom logic
//NOTE: allocate in order of appearance (preserve ordering)
    scan.allocque.forEach(symbol =>
    {
//        if (symbol.final) return; //addr/value already finalized
        scan.linenum = symbol.linenum;
//        if (symbol.how == "dabs")
//        if (symbol.how == "label")
        if (!Object.keys(scan.segs).some(name =>
        {
            var seg = scan.segs[name];
            if (IsProg(seg)) return false; //assign data addresses before code gen, since code size will depend on bank selects
            if ((symbol.value < seg.start) || (symbol.value >= seg.start + seg.limit)) return false; //not this memory area
            if (symbol.value != seg.start) return true; //already allocated (explicitly)
            if (!seg.items) seg.items = []; //console.log("no items:", seg);
            symbol.value = seg.start + seg.items.length; //allocate next available
            seg.items.length += symbol.size || 1;
            if (seg.items.length > seg.limit) symberr("Memory area full", symbol.name);
//            symbol.final = true;
            return true; //stop searching
        })) symberr("Don't know how to alloc memory for", symbol.name);
    });

    console.log("allocated", scan.allocque);
//TODO:
//remove: pagesel; goto $+1
//chain: goto x; x: goto y
//reduce: movwf temp,x; movwf param,x
//chain: call x -> call y
//swap: btf; goto x; goto y; x:
//remove: unused labels, then unreachable code (label not used + can't fall thru)
    return true;
}


function x_fixup()
{
    console.log("asm fixup now".green_lt);

//remove useless jumps:
    var useless_goto = 0, conditional, org_stack = [];
    scan.lines.forEach((line, inx, all) =>
    {
debugger;
        var parts;
//        if (!has_opcode(line)) return; //continue
        if (line.match(/^s+(btfsc|btfss|decfsz|incfsz)\s/i)) { conditional = inx; return; }
        if (conditional || !(parts = line.match(/^\s+(goto|ljmp)\s+([a-z0-9@?_]+)\s+(;\/\/CODE\s+0x[0-9a-f]+\s)/i))) { conditional = false; return; }
        var target_re = new RegExp("^" + RegEsc(parts[2]) + ":\\s+" + RegEsc(parts[3]), "i");
//if (inx == 1250) console.error("found uncond goto/ljmp to '%s' from '%s' at line %d", parts[2], parts[3], inx, "^" + RegEsc(parts[2]) + ":\\s+" + RegEsc(parts[3]));
        for (var i = inx + 1; i < all.length; ++i)
        {
            var line = all[i];
//console.error("compare line '%s' to target '%s' %s", line, target, next_adrs);
//if (parts = line.match(/^\s+org\s+(\d)+/i))
//if (i == 1253) console.error("match? '%s'", line);
            if (line.match(target_re))
            {
//console.error("found matching label at line %d: '%s'".cyan_lt, i, line);
                all[inx] = ";REDUNDANT-GOTO " + all[inx];
                ++useless_goto;
                break;
            }
//allow            if (line.match(/^\s+org\s/i)) break; //non-contiguous code
//has_opcode.debug = i;
            if (has_opcode(line)) break; //{ if (line == 1254) console.error("no matching; found opc '%s' at line %d".cyan_lt, line, i); break; }
        }
    });

//comment out unused labels:
    var equs = " " + scan.equs.join(" ") + " ";
    console.log("equs %s".cyan_lt, equs);
    var unused_labels = 0;
    scan.lines.forEach((line, inx, all) =>
    {
//debugger;
        var parts = line.match(/^([a-z0-9@?_]+):\s/i);
        if (!parts) return; //continue
        var symb = scan.symbols[parts[1]];
        if (!symb) throw `Unknown label '${parts[1]}' at line ${inx + 1}`;
        if (symb.used) return;
        if (equs.match("[^a-z0-9@?_]" + parts[1] + "[^a-z0-9@?_]")) { symb.used = true; return; } //continue
        if (symb.class != "CODE") return;
        all[inx] = ";UNUSED " + line;
        ++unused_labels;
    });


//remove unreachable code:
    var unreachable = 0;
    scan.lines.forEach((line, inx, all) =>
    {
debugger;
        var parts;
//        if (!has_opcode(line)) return; //continue
        if (line.match(/^s+(btfsc|btfss|decfsz|incfsz)\s/i)) { conditional = inx; return; }
        if (conditional || !(parts = line.match(/^\s+(goto|ljmp)\s+([a-z0-9@?_]+)\s+(;\/\/CODE\s+0x[0-9a-f]+\s)/i))) { conditional = false; return; }
        var target_re = new RegExp("^" + RegEsc(parts[2]) + ":\\s+" + RegEsc(parts[3]), "i");
//console.error("found goto/ljmp to '%s' at line %d", parts[2], inx);
        for (var i = inx + 1; i < all.length; ++i)
        {
            var line = all[i];
            if (line.match(target_re))
            {
//console.error("found matching label at line %d: '%s'".cyan_lt, i, line);
                all[inx] = ";REDUNDANT-GOTO " + all[inx];
                ++useless_goto;
//TODO: update ref count; maybe comment unused label
                break;
            }
            if (line.match(/^\s+org\s/i)) break; //non-contiguous
            if (line.match(/^([a-z0-9@?_]+):\s/i)) break; //reachable if label is not commented out
            if (!has_opcode(line)) continue;
            all[i] = ";UNREACHABLE " + all[i];
            ++unreachable;
        }
    });

//remove redundant WREG loads:
//	movwf	name ;comment
//	movf	name,w ;comment
    var redundant_movfw = 0;
    scan.lines.forEach((line, inx, all) =>
    {
debugger;
        var parts;
        if (line.match(/^s+(btfsc|btfss|decfsz|incfsz)\s/i)) { conditional = inx; return; }
        if (conditional || !(parts = line.match(/^\s+movwf\s+([a-z0-9@?_]+)\s/i))) { conditional = false; return; }
//        inx = find(inx, "^\\s+movf\\s+" + RegEsc(parts[1]) + "\\s*,\\s*w\\s+", "i");
        var target_re = new RegExp("^\\s+movf\\s+" + RegEsc(parts[1]) + "\\s*,\\s*w\\s+", "i");
//    scan.lines.slice(stinx + 1, stinx + 10).forEach((line, inx) =>
        for (var i = inx + 1; i < all.length; ++i)
        {
            var line = all[i];
            if (line.match(target_re))
            {
                all[i] = ";REDUNDANT-MOVF " + all[i];
                ++redundant_movfw;
                break;
            }
//allow            if (line.match(/^\s+org\s/i)) break; //non-contiguous code
//has_opcode.debug = i;
        	if (line.match(/^([a-z0-9_@\?]+):\s+/i)) break; //could jump in with other value
            if (has_opcode(line)) break; //{ if (line == 1254) console.error("no matching; found opc '%s' at line %d".cyan_lt, line, i); break; }
        }
//console.log("re '%s' not found at line %d..", pattern, stinx);
    });

//remove redundant WREG save:
//	movlw	# ;comment
//  movlb   0 ;comment
//	movwf	9 ;comment
    var redundant_movwf = 0;
    scan.lines.forEach((line, inx, all) =>
    {
debugger;
        if (line.match(/^\s+movwf\s+9\s/i))
        {
            if (all[inx-1].match(/^\s+movlb\s+0\s/))
            {
                all[inx - 1] = ";REDUNDANT-MOVWF " + all[inx - 1];
                ++redundant_movfw;
            }
            all[inx] = ";REDUNDANT-MOVWF " + all[inx];
            ++redundant_movfw;
        }
    });


    if (unused_labels) console.log("#unused labels removed: %d".green_lt, unused_labels);
    if (unreachable) console.log("#unreachable opcodes removed: %d".green_lt, unreachable);
    if (useless_goto) console.log("#useless goto removed: %d".green_lt, useless_goto);
    if (redundant_movfw) console.log("#redundant movfw removed: %d".green_lt, redundant_movfw);
    if (redundant_movwf) console.log("#redundant movwf removed: %d".green_lt, redundant_movwf);

    var asmfile = __dirname + `/output/${basefile}.as`;
    var outfile = fs.createWriteStream(asmfile + "m");
    scan.lines.forEach((line, inx, all) =>
    {
//        if (!inx) leader(outfile);
//        console.log(inbuf);
//        var newbuf = ignore(inbuf) || rewrite(inbuf) || inbuf;
//        if (typeof newbuf != "string") console.error("wrong line type:", typeof newbuf, newbuf);
//        outfile.write(line + "\n");
//        if (inx == all.length - 1) { outfile.write(reduce(lines).join("\n")); trailer(outfile); }
    });
    leader(outfile);
//console.log("scan line 0", scan.lines[0].charCodeAt(scan.lines[0].length - 1), scan.lines[0].length);
    outfile.write(scan.lines.join("\n"));
    trailer(outfile);
}


function leader(outfile)
{
    outfile.write(`
	title  "${basefile}.c assembly fixup ${Date.now().toString()}"
	LIST n=60, c=200, b=004, n=0; //line (page) size, column size, tab width, no paging
	LIST r=dec; //RADIX DEC; //match xc8 radix to avoid rewriting consts
	LIST mm=on; //memory map
	LIST st=on; //symbol table
    LIST t=on; //truncate (vs wrap)
;//    LIST w=-230; //message level [0..2]
;//    LIST x=on; //macro expansion
;//    errorlevel -127 //no worky
;//    errorlevel -128 //no worky
	errorlevel -302; //this is a useless/annoying message because MPASM doesn't handle it well (always generates warning when accessing registers in bank 1, even if you've set the bank select bits correctly)
	errorlevel -306; //this is a useless/annoying message because MPASM doesn't handle it well (always generates warning when accessing page 1, even if you've set the page select bits correctly)

#define REG_PAGE_SIZE  0x100; //code at this address or above is paged and needs page select bits (8 bit address)
#define LIT_PAGE_SIZE  0x800; //code at this address or above is paged and needs page select bits (11 bit address)
;//get page# of a code address:
#define LITPAGEOF(dest)  ((dest)/LIT_PAGE_SIZE); //used for direct addressing (thru opcode)
#define REGPAGEOF(dest)  ((dest)/REG_PAGE_SIZE); //used for indirect addressing (thru register)

;//frequently used boolean constants:
	CONSTANT TRUE = 1
	CONSTANT FALSE = 0
#define TF01(val)  ((val)!=0)  ;//turn int into 0/1
#define FT01(val)  ((val)==0)  ;//turn int into 1/0

;//ternary inline-if operator (like C/C++ "?:" operator):
#define IIF(tfval, tval, fval)  (TF01(tfval)*(tval) + FT01(tfval)*(fval))
#define IIF0(tfval, tval, fval)  (TF01(tfval)*(tval))  ;//shorter version if fval == 0; helps avoid MPASM line length errors
	if (((3 == 3) != TRUE) || ((3 != 3) != FALSE))
        error [ERROR] "IIF" is broken: true == #v(3 == 3) and false == #v(3 != 3)  ;//paranoid self-check
    endif

#define isPASS2  eof

fcall MACRO dest
    expand
;;    pagesel LITPAGEOF(dest)
    pagesel dest
    call dest
    ENDM

ljmp MACRO dest
    expand
;;    if (dest != $+1)
;;        P2PAGESEL dest
;;        goto dest
;;    endif
;;    pagesel LITPAGEOF(dest)
    pagesel dest
    goto dest
    ENDM

start: ;//need to define this symbol
`);
}


function trailer(outfile)
{
    outfile.end(`
eof: ; //this must go AFTER all executable code (MUST be a forward reference); used to detect pass 1 vs. 2 for annoying error[116] fixups
	if !LITPAGEOF(eof)
        messg "[INFO] No need to save/restore PCLATH; code is all on same page (eof @ " #v(eof) ""
    endif
    END; //MPASM wants this @eof
`);
}


///////////////////////////////////////////////////////////////////////////////
////
/// misc helpers
//


function mydir(relpath)
{
    return relpath.replace(/^\./, __dirname);
}

function count(obj, limit)
{
//    var retval = 0;
//    if (obj)
//        for (var i in obj)
//        {
//            ++retval;
//            if (limit && (retval >= limit)) break;
//        }
//    return retval;
    return Object.keys(obj).length;
}


function isnum(val) { return !isNaN(val); }


/*no worky
function RegEsc(str)
{
//see https://stackoverflow.com/questions/3561493/is-there-a-regexp-escape-function-in-javascript/3561711#3561711
    return str.replace(/(?=.)/g, '\\');
}
*/


//no; async write! process.exit(
main(); //put at end to avoid hoist errors

//eof
