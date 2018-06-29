#!/usr/bin/env node
//clean up sdcc .asm output
//- for readability
//- syntax clean up for MPASM
//- apply optimizations for more efficient code

//History:
// 3/4/13 DJ added SWOPCODE for WS2811 on PIC16F1827
// 3/24/13 DJ added MOVIW, MOVWI
// 4/12/13 DJ added ABSDIFF for 8-bit jump offset mgmt
// 4/25/13 DJ added ATADRS to allow more precise placement of code
// 5/4/13  DJ added LEZ
// 3/20/14 DJ added indf1_auto* for parallel ws2811
// 5/20/17 DJ rewrote in node.js; reworked tool chain (replaced proprietary/licensed SourceBoost with free Microchip HTC/PICC-Lite/XC8 compiler)
// 11/30/17 DJ switch to SDCC (it supports "inline", whereas free version of XC8 does NOT), rework as streams
// 6/10/18 DJ merge grep/sed fixup into here

"use strict";
require("colors").enabled = true; //for console output; https://github.com/Marak/colors.js/issues/127
const fs = require("fs");
const thru2 = require("through2"); //https://www.npmjs.com/package/through2
const {LineStream} = require('byline');


var [instm, outstm] = [process.stdin, process.stdout];
//if (instm.isTTY) instm = fs.createReadStream("build/ssr-ws281x.asm");
//fs.createReadStream(process.argv[2] || "(no file)")
instm
    .pipe(new LineStream()) //{keepEmptyLines: false}))
    .pipe(asm_cleanup())
    .pipe(asm_optimize())
//    .pipe(text_cleanup())
    .pipe(outstm);


//first clean-up pass:
function asm_cleanup()
{
    return thru2(xform, flush); //{ objectMode: true, allowHalfOpen: false },

    function xform(chunk, enc, cb)
    {
        if (isNaN(++this.numlines)) this.numlines = 1;
        if (typeof chunk != "string") chunk = chunk.toString(); //TODO: enc?
//        chunk = chunk.replace(/[{,]\s*([a-z\d]+)\s*:/g, (match, val) => { return match[0] + '"' + val + '":'; }); //JSON fixup: numeric keys need to be quoted :(
        if (chunk.length)
            switch (keep.call(this, chunk))
            {
                case false: break; //discarded
                case true: if (isNaN(++this.kept)) this.kept = 1; break; //kept (might have been altered)
//                default: this.push(`; ${chunk.replace(/;.*$/, "")}\n`); if (isNaN(++this.reduced)) this.reduced = 1; break; //optimized out (commented); preserve line to make it easier to validate optimization
                default: this.push(`;P0 ${chunk.replace(/;id=\d+,.*$/i, "")}\n`); if (isNaN(++this.reduced)) this.reduced = 1; break; //optimized out (commented); preserve line to make it easier to validate optimization
            }
        cb(); //null, chunk);
    }

    function flush(cb)
    {
        var total = (this.kept || 0) + (this.reduced || 0);
        this.push(`;fixup stats ${new Date().toLocaleString()}\n`);
        this.push(`;pass 1: #lines ${this.numlines || 0}, #kept ${this.kept || 0} (${percent(this.kept, total)}%), #reduced ${this.reduced || 0} (${percent(this.reduced, total)}%), banksel keep ${this.banksel_keep || 0}, toss ${this.banksel_toss || 0}\n`);
        cb();
    }

    function keep(buf)
    {
        const WANT_PAGESEL = false; //don't need this if everything fits in one code page :)
        const WANT_BANKSEL = false; //TODO: further optimization
        const neither = 2; //not true or false (tri-state)
        var parts;

        buf = buf.replace(/[^a-z0-9_]status[^a-z0-9_]/i, str => str.slice(0, 1) + "_" + str.slice(1)); //kludge: fix bad symbol name
//;; BANKOPT2 BANKSEL dropped; xyz present in same bank as abc
//recover all BANKSEL here and decide in next pass whether they are needed
        if (parts = buf.match(/^;; BANKOPT2 BANKSEL dropped; ([a-z0-9_]+) present in same bank as ([a-z0-9_]+)\s*$/i)) // (.*)$/i))
            if (parts[1] == parts[2]) { if (isNaN(++this.banksel_toss)) this.banksel_toss = 1; return true; } //false; }
            else { if (isNaN(++this.banksel_keep)) this.banksel_keep = 1; this.push(`\tBANKSEL ${parts[1]} ;;UU !bank of ${parts[2]} ${buf.slice(2)}\n`); return true; } //kludge: undo sdcc bug; TODO: submit sdcc bug report (non-banked regs treated as banked, then subsequent bank checks are wrong)
        if (buf.match(/^\s*;/)) return false; //strip *lots of* comments
        if (buf.match(/^\s+extern\s/i)) return false;
        if (buf.match(/^\s+global\s/i)) return false;
//        if (buf.match(/\scode(\s|$)/i)) { this.push("\n"); return false; } //insert blank line for readabililty
        if (buf.match(/\scode(\s|$)/i)) this.push("\n"); //insert blank line (for debug readabililty)
//        if (buf.match(/\scode\s*$/i)) return false;

        if (buf.match(/\s+movwf\s+_wreg\s*(;|$)/i)) return neither; //TODO: check for a STATUS bit check following
//        if (!WANT_BANKSEL && buf.match(/\s+banksel\s+_wreg\s*(;|$)/i)) return neither; //TODO: check for a STATUS bit check following
        if (!WANT_PAGESEL && buf.match(/\s+pagesel\s/i)) return neither; //not needed for single code page
        if (parts = buf.match(/^([a-z0-9_]+)(\s+code)(\s+0x[0-9a-f]+)?\s*(;.*)?$/i)) //rewrite code seg as label + org
        {
            if (!parts[3]) { if (!keep.reloc) parts[3] = "0x400"; keep.reloc = true; } //kludge: reloc away from startup vector first time
            if (parts[3]) { this.push(`\torg ${parts[3]} ;;C1 ${buf}\n`); buf = buf.replace(parts[3], ""); }
            buf = parts[1] + ":" + (parts[4] || "");
        }
        buf = buf.replace(/;id=\d+,.*$/i, ""); //TODO: is there useful info in here?
        this.push(buf + "\n"); //include newline to preserve line-based syntax
        return true;
    }
};


//second clean-up pass:
function asm_optimize()
{
    return thru2(xform, flush); //{ objectMode: true, allowHalfOpen: false },

    function xform(chunk, enc, cb)
    {
        if (!this.lines) this.lines = [];
        if (typeof chunk != "string") chunk = chunk.toString(); //TODO: enc?
//buffer everything for analysis since source has already been trimmed:
//TODO: maybe only buffer ~ 10 lines and use "peep-hole" optimization (in the spirit of streaming)
        if (chunk.length) this.lines.push(chunk);
        cb(); //null, chunk);
    }

    function flush(cb)
    {
        var altered = reduce(this.lines);
        var newlines = this.lines;
        var commented = 0;
        newlines.forEach(line => { if (line.match(/^\s*;/)) ++commented; this.push(line); });
//        this.push(`;#lines: ${this.numlines || 0}, #kept: ${this.kept || 0} (${percent(this.kept, total)}%), #reduced: ${this.reduced || 0} (${percent(this.reduced, total)}%)\n`);
        this.push(`;pass 2: #kept ${newlines.length} (${percent(newlines.length, this.lines.length)}%), #dropped ${this.lines.length - newlines.length} (${percent(this.lines.length - newlines.length, this.lines.length)}%), #commented ${commented} (${percent(commented, this.lines.length)}%), #altered ${altered} (${percent(altered, this.lines.length)}%)\n`);
        cb();
    }

    function reduce(lines)
    {
        const labeldef = /^([a-z0-9_]+)\s*:\s*(;|$)/i;
        const MAX_LOOKBACK = 150; //30; //don't make it too high (hurts perf)
//        var newlines = [];
        var altered = 0;
        lines.comment = function(inx, reason) //TODO: tag with reason code? (for easier debug)
        {
//            if (!chkdup || (this[inx].indexOf(";; ") == -1)) this[inx] = ";; " + this[inx];
            if (!this[inx].match(/^\s*;;/)) this[inx] = `;;${reason || ""} ` + this[inx];
        }
        lines.lookback = function(pattern, inx, prevent_other, want_debug)
        {
            const retdebug = want_debug? function(where, why) { console.log("lb[%d]: %s", where, why, this[where].trimln()); }.bind(this): function() {};
            delete this.backinx; //delete previous results
            for (var found = inx - 1; (found >= inx - MAX_LOOKBACK) && (found >= 0); --found)
            {
                if (this.backparts = this[found].match(pattern)) { retdebug(found, "match"); return this.backinx = found; } //put this one first in case it overlaps other patterns below
                if (this[found].match(/^\s*(;|$)/)) { retdebug(found, "comment"); continue; } //skip commented or blank lines
                if (this[found].match(/^\s+callw?\s/i)) return retdebug(found, "call"); //bank unknown after return from function
//                if (this[found].match(/^[a-z0-9_]+\s*:/i)) return retdebug(found, "label"); //execution could enter at label so cancel look-back
                if (this[found].match(labeldef)) return retdebug(found, "label"); //execution could enter at label so cancel look-back
                if (prevent_other === 1) prevent_other = true; //just allow one stmt
                else if (prevent_other) return retdebug(found, "other"); //cancel look-back for anything else
            }
            retdebug(-1, "!found");
//            return false; //pattern not found within range
        }

        var symtab = {}, labels = {};
        symtab.bankof = function(varname, where)
        {
            const NonBankedFSR =
            {
                _INDF: true, _PCL: true, _STATUS: true, _FSR: true, _PCLATH: true, _INTCON: true, //16F688 unbank regs < 0x0C
                _STATUSbits: true, _INTCONbits: true, //sdcc uses alternate names for bitized vars
                _INDF0: true, _INDF1: true, _FSR0L: true, _FSR0H: true, _FSR1L: true, _FSR1H: true, _BSR: true, _WREG: true, //16F182x all are unbanked < 0x0C
            };
            if (!(varname in this)) console.error(`Undefined var: '${varname}' at line ${where}`.red_lt);
            var adrs7 = this[varname] & 0x7F;
//CAUTION: 16F688 has *some* banked regs < 0x0C; 16F182x has all non-banked regs < 0x0C
            if (NonBankedFSR[varname]) adrs7 = 0xff; //kludge: treat as non-banked
            return (/*(adrs7 >= 0x0c) &&*/ (adrs7 < 0x70))? this[varname] & ~0x7F: -1;
        }
//initial pass to discover jump targets (labels):
        lines.forEach((line, inx, all) =>
        {
            var parts;
//            line = line.replace(/;.*$/, ""); //remove comments to prevent spurious pattern matches
//label states:
// < 0 == defined (location), not used
// = 0 == used, unknown location
// > 0 == defined (location), used
//NOTE: labels might be used before they are defined
//            if (parts = line.match(/^([a-z0-9_]+)\s*:/i)) //definition
            if (parts = line.match(labeldef)) //definition
//                labels[parts[1]] = -inx; //remember where it was defined, mark unused; //all[inx + 1]; //remember next instr for each label
            {
                var name = parts[1];
//                if (labels[name] === 0) labels[name] = inx; //used: now remember location
//                else if (labels[name]) console.error(`Duplicate label; '${name} at line ${inx + 1}'`.red_lt);
//                else labels[name] = -inx; //unused: remember location
                if (!labels[name]) labels[name] = {};
                if (labels[name].definition) console.error(`Duplicate label; '${name} at line ${inx + 1}'`.red_lt);
                else labels[name].definition = inx; //can't be 0
            }
            if (parts = line.match(/^\s(callw?|goto|braw?)\s+([a-z0-9_]+)(\s|;|$)/i)) //usage
            {
                var name = parts[2];
//                if (labels[name] < 0) labels[name] = -labels[name]; //mark it used (known location)
//                else if (labels[name] > 0); //noop; already marked used
//                else labels[name] = 0; //mark used (unknown location)
                if (!labels[name]) labels[name] = {};
                if (isNaN(++labels[name].numrefs)) labels[name].numrefs = 1;
            }
        });
//remove unused labels (reduce code entry points, improves optimization):
        Object.keys(labels).forEach(name =>
        {
            var inx = -2; //TODO
            if (!labels[name].definition) console.error(`Undefined label: '${name}' at line ${inx + 1}`.red_lt);
            if (!labels[name].numrefs) lines.comment(labels[name].definition, "U1"); //remove unneeded label (improves optimizations)
//TODO: remove single-use labels that follow normal flow anyway
        });

//apply optimizations:
        const goaway = /^\s+(goto\s|braw?\s|return\s*$|(retlw)\s|retfie\s*$)/i; //won't come back
        var varadrs, nxtadrs = 0x20; //RAM_START
        lines.forEach((line, inx, all) =>
        {
            var parts;
//            if (!all.init && line.match(/^\s+nop/i)) //first opcode; TODO: better way to detect
//            var line_pair = inx? all[inx - 1] + line: "";
//allocate storage space for variables:
            if (parts = line.match(/^[a-z0-9_]+?\s+udata_ovr\s+(0x[0-9a-f]+)\s*$/i)) //"at" predefined adrs
            {
                varadrs = -parseInt(parts[1].slice(2), 16); //use negative for fixed (pre-defined) address
                all.comment(inx, "S1");
//                if (all.init) return;
//                all[inx] = "\torg 0 ;;G0 first opc\n\tCLRF PCLATH ;;G1 first code page\n" + all[inx];
//                all.init = true;
                return;
            }
            if (line.match(/^[a-z0-9_]+?\s+udata\s*$/i)) //use next sequential adrs as-is
            {
                varadrs = nxtadrs;
                all.comment(inx, "S2");
//                if (all.init) return;
//                all[inx] = "\torg 0 ;;G2 first opc\n\tCLRF PCLATH ;;G3 first code page\n" + all[inx];
//                all.init = true;
                return;
            }
//            if (parts = line.match(/^([a-z0-9_]+)\s+res\s+(\d+)/i))
            if (parts = line.match(/^([a-z0-9_]+)(\s|$)/i)) //assign storage to data label
            {
                symtab[parts[1]] = Math.abs(varadrs);
                all[inx] = `${parts[1]}\tequ 0x${Math.abs(varadrs).toString(16)} ;;S3 ` + all[inx]; //assign adrs to symbol; NOTE: could be multiple; don't change "line"
                if (parts[1].match(/^_wreg$/i) && (Math.abs(varadrs) >= 0x70)) //fake ref def for older PICs; remove
                {
                    all.comment(inx, "U5"); //leave undefined to catch optimization errors
                    return; //don't alloc "res" space for unneeded vars
                }
                if (parts[1].match(/^(_indf\d_(preinc|predec|postinc|postdec)|_fake_bits|_labdcl|_swopcode|[psw]save|stk\d+|r0x\d+)$/i))
                {
                    all.comment(inx, "U2"); //leave undefined to catch optimization errors
                    return; //don't alloc "res" space for unneeded vars
                }
            }
//NOTE: line might be split; "res" parsing continues from above
//            if (parts = line.match(/^[a-z0-9_]+?\s+res\s+(\d+)/i)) //allocate space; non-greedy
            if (parts = line.match(/^([a-z0-9_]+)?\s+res\s+(\d+)/i)) //allocate space
            {
                varadrs += Math.sign(varadrs) * parts[2];
                if (varadrs > 0) nxtadrs = varadrs;
//if (isNaN(++reduce.count)) reduce.count = 1;
//if (reduce.count < 50) console.log("res[%d] before", inx, all[inx].indexOf(";;S3"), all[inx].trimln());
                if (all[inx].indexOf(";;S3") == -1) all.comment(inx, "S4"); //don't comment if already commented above
//if (reduce.count < 50) console.log("res[%d] after", inx, all[inx].trimln());
                return;
            }
//            if (!all.init && line.match(/^\s+nop\s*(;|$)/i)) //first opcode line; TODO: find better way to detect this
//            {
//                all[inx] = "\torg 0 ;;G2 first opc\n" + all[inx] + "\tCLRF PCLATH ;;G3 first code page\n"; //leave first instr as "nop" for debuggers
//                all.init = true;
//                return;
//            }
//            if (typeof all.init == "undefined") //kludge: SDCC can put other code ahead of startup; need to relocate it
//            {
//                all[inx] = "\torg 0x400 ;;G5 reloc pre-startup code\n" + all[inx];
//                all.init = false;
//                //continue parsing
//            }
//remove labdcls (only used for code gen debug):
            if (line.match(/^[^;]*\s+_labdcl(\s|$)/i)) //do this before storage allocation
            {
//if (isNaN(++reduce.count)) reduce.count = 1;
                if (all.lookback(/^\s+movlw\s/i, inx, true)) all.comment(all.backinx, "D1");
//if (reduce.count < 5) console.log("labdcl%d]", inx, all.backinx, all[inx - 1].match(/\s+movlw\s/i));
                all.comment(inx, "D2");
                return;
            }
//remove always/never (used to avoid "unreachable code" compiler warnings):
            if (parts = line.match(/^\s+btf(ss|(sc))\s+_fake_bits,0(\s|$)/i)) //do this before label/jump optimization
            {
//                if (all.lookback(/^\s+movlw\s/i, inx, true)) all.comment(all.backinx, "D1");
                all.comment(inx, "F1"); //remove conditional check
                if (parts[2]) all.comment(inx + 1, "F2"); //"never" will always be clear; remove following stmt also
                return;
            }
//set banksel optimization level: NO- examine code
//            if (line.match(/^[^;]*\s+_numbanks(\s|$)/i)) //do this before storage allocation
//            {
//                if (all.lookback(/^\s+movlw\s/i, inx, true)) all.comment(all.backinx, "D1");
//                all.comment(inx, "D2");
//                return;
//            }
//dangling returns:
            const conditionals = /^\s+(btfss|btfsc|decfsz|incfsz)\s/i;
            if (parts = line.match(goaway)) //NOTE: falls thru to other parsing
                if (all.lookback(goaway, inx, true))
                {
                    var svbackparts = all.backparts;
                    if (!all.lookback(conditionals, all.backinx, true))
                    {
                        if (parts[2] && svbackparts[2]); //don't comment out lookup table entries
                        else all.comment(inx, "G4"); //drop redundant go-away instr
                        return;
                    }
                }
//symbol fixups:
//            if (parts = line.match(/^\s+(goto|bra)\s+__sdcc_gsinit_startup\s*$/i)) //special case
//            {
//                all[inx] = `\t${parts[1]} _main;; ${line}`;
//                return;
//            }
//drop redundant banksels:
//TODO: push banksel before loop/label if only path is following jump
            const banksel = /^\s+banksel\s+\(?([a-z0-9_]+)(\+\d+\))?\s*(;|$)/i; //CAUTION: ignore offset; assume no bank spanning
            if (parts = line.match(banksel))
            {
                var varname = parts[1], newbank = symtab.bankof(varname, inx + 1);
                if (newbank == -1) { all.comment(inx, "B1"); return; } //this banksel not needed
//                if (all.lookback(/^[a-z0-9_]+\s*:/i, inx, true)) return; //keep first banksel after label, even if not needed
                if (all.lookback(labeldef, inx, true)) return; //keep first banksel after label, even if not needed
                if (newbank == -1) all.comment(inx, "B2"); //doesn't need banksel
                else if (all.lookback(banksel, inx) && (symtab.bankof(all.backparts[1], inx + 1) == newbank)) all.comment(inx, "B3"); //drop redundant banksel
                return;
            }
//reduce jump or return chains:
            if (parts = line.match(/^\s+(goto|bra)\s+([a-z0-9_]+)\s*$/i)) //TODO: "braw" if destination is "retlw"
//                if (all[labels[parts[2]].definition + 1].match(goaway))
                for (var fwdinx = labels[parts[2]].definition + 1;; ++fwdinx)
                {
                    if (all[fwdinx].match(/^\s*(;|$)/)) continue; //skip commented or blank lines
//                    if (all[fwdinx].match(/^[a-z0-9_]+\s*:/i)) continue; //skip labels
                    if (all[fwdinx].match(labeldef)) continue; //skip labels
                    if (!all[fwdinx].match(goaway)) break;
                    all[inx] = all[fwdinx].trimln() + ";;R1 " + line; //update "line" for further optimization
//no                    return; //look for other optimizations
                    if (labels[parts[2]].numrefs == 1) all.comment(labels[parts[2]].definition, "U3"); //remove unneeded label
                    return;
                }
//TODO: fix this for SerialErrorReset
            if (line.match(/^\s+(return|retfie)\s*(;|$)/i)) //no return value
                if (all.lookback(/^\s+callw?\s+([a-z0-9_]+)\s*$/i, inx, true))
                {
                    all[all.backinx] = `\tgoto ${all.backparts[1]};;R2 ` + all[all.backinx]; //bypass extraneous "return" level
                    all.comment(inx, "R3");
                    return;
                }
//remove useless instr:
//NOTE: SDCC seems to forget that WREG == W sometimes
            if (line.match(/^\s+MOVF\s+_WREG\s*,\s*W\s*$/i))
            {
                all.comment(inx, "W1");
                return;
            }
//replace explicit WREG op with implicit (for compatibility with non-PIC16X):
            if (parts = line.match(/^\s+((incf)|decf)\s+_WREG\s*,\s*F\s*$/i))
            {
                all[inx] = `\taddlw ${parts[2]? "1": "0xff"} ;;W2 ` + all[inx];
                return;
            }
            if (parts = line.match(/^\s+(clrf)\s+_WREG\s*$/i))
            {
                all[inx] = `\tmovlw 0 ;;W3 ` + all[inx];
                return;
            }
//invert bit test and remove extraneous singleton jump:
//	BTFSC	bitvar,bitnum
//	GOTO	around
//	BANKSEL	bitvar2 //TODO: this case no worky
//	single-instr
//around:
//TODO: btfsc/ret/inc/ret -> btfss/inc/ret
            if (parts = line.match(labeldef))
                if (labels[parts[1]].numrefs == 1) //candidate for elimination
                {
                    const InvertOpcs = {btfsc: "btfss", btfss: "btfsc"};
                    var name = parts[1];
                    var bsinx = all.lookback(banksel, inx, 1), jumpbank = bsinx? symtab.bankof(all.backparts[1], bsinx + 1): -1;
                    var svinx = all.lookback(new RegExp("^\\s+goto\\s+" + name + "(\\s|$)", "i"), bsinx || inx, bsinx? true: 1);
                    if (svinx && all.lookback(/^\s+(btfss|btfsc)\s+\(?([a-z0-9_]+)(\+\d+\))?/i, all.backinx, true))
                        if (!bsinx || (symtab.bankof(all.backparts[2], all.backinx + 1) == jumpbank))
                        {
//                            console.log("flip backparts", all.backparts);
                            all[all.backinx] = `${all[all.backinx].replace(all.backparts[1], InvertOpcs[all.backparts[1].toLowerCase()]).trimln()};;I1 ` + all[all.backinx];
                            if (bsinx) { all[all.backinx] = all[bsinx].trimln() + " ;;I2 moved up from below\n" + all[backinx]; all.comment(bsinx, "I3"); }
                            all.comment(svinx, "I4");
                            all.comment(inx, "I5");
                            delete labels[name];
//fall thru for more optimization (decfsz)
                        }
                }
//use decfsz:
//TODO: look for incfsz
            if (line.match(/^\s+btfss\s+_status\s*,\s*2\s*(;|$)/i)) //skip if ZERO
                if (parts = all[inx - 1].match(/^\s+iorwf\s+([a-z0-9_]+)\s*,\s*w\s*(;|$)/i))
                    if (all[inx - 2].match(/^\s+movlw\s+0(x00)?\s*(;|$)/i))
                        if (parts = all[inx - 3].match(new RegExp("^\\s+decf\\s+(" + parts[1] + ")\\s*,\\s*([fw])\\s*(;|$)", "i")))
                        {
                            all[inx - 3] = `\tdecfsz ${parts[1]},${parts[2]} ;;K1 ` + all[inx - 3];
                            all.comment(inx - 2, "K2");
                            all.comment(inx - 1, "K3");
                            all.comment(inx, "K4");
                            return;
                        }
//TODO:                        else replace movlw 0 + iorwf with movf,w
//indf pre/post inc opcodes (PIC16X):
            if (parts = line.match(/^\s+(movwf|movf|clrf)\s+_indf(\d)_(preinc|predec|postinc|postdec)(,W)?\s*$/i))
            {
                const FancyOpcodes = {movwf: "MOVWI", movf: "MOVIW", clrf: "MOVLW 0\n\tMOVWI"};
                const PrePostIncDec = {preinc: "++FSR#", predec: "--FSR#", postinc: "FSR#++", postdec: "FSR#--"};
//TODO                var needclw = all.lookback(/^\s+movlw\s+0\s*(;|$)/i, inx);
//                var newopc;
//NOTE: movwi/moviw ++/-- is 16-bit
//		        switch (parts[1] + " " + parts[3])
//		        {
//			        case "MOVWF PREINC": newopc = "MOVWI ++FSR#"; break;
//			        case "MOVWF PREDEC": newopc = "MOVWI --FSR#"; break;
//			        case "MOVWF POSTINC": newopc = "MOVWI FSR#++"; break;
//			        case "MOVWF POSTDEC": newopc = "MOVWI FSR#--"; break;
//			        case "MOVF PREINC": newopc = "MOVIW ++FSR#"; break;
//			        case "MOVF PREDEC": newopc = "MOVIW --FSR#"; break;
//			        case "MOVF POSTINC": newopc = "MOVIW FSR#++"; break;
//			        case "MOVF POSTDEC": newopc = "MOVIW FSR#--"; break;
//			        case "CLRF PREINC": newopc = "MOVLW 0\n\tMOVWI ++FSR#"; break;
//			        case "CLRF PREDEC": newopc = "MOVLW 0\n\tMOVWI --FSR#"; break;
//			        case "CLRF POSTINC": newopc = "MOVLW 0\n\tMOVWI FSR#++"; break;
//			        case "CLRF POSTDEC": newopc = "MOVLW 0\n\tMOVWI FSR#--"; break;
//			        default: console.error(`unhandled indirect: ${parts[1] + " " + parts[3]}`.red_lt);
//		        }
                all[inx] = `\t${FancyOpcodes[parts[1].toLowerCase()]} ${PrePostIncDec[parts[3].toLowerCase()].replace(/#/gi, parts[2])};;F1 ` + all[inx];
            }
//TODO: add opc operand swapping to compensate for SDCC incorrect code gen
//TODO: look for chained conditionals
//TODO: convert GOTO to BRW? (no advantage?)
//TODO: if PAGESEL and no intervening call or label, drop it
//TODO: drop jump to following; does this occur?
//TODO: remove unreachable code
//TODO: merge MOVLW and instr following that manip only WREG
//TODO: reduce/remove RP0, RP1 (banksel) if caller wants only 1 - 2 banks
        });
//do another pass in case we can do more:
//this catches _sdcc_gsinit_startup (requires call/return reduction first)
        lines.forEach((line, inx, all) =>
        {
            var parts;
//reduce jump or return chains:
//TODO: DRY this
            if (parts = line.match(/^\s+(goto|bra)\s+([a-z0-9_]+)\s*$/i)) //TODO: "braw" if destination is "retlw"
//                if (all[labels[parts[2]].definition + 1].match(goaway))
                for (var fwdinx = labels[parts[2]].definition + 1;; ++fwdinx)
                {
                    if (all[fwdinx].match(/^\s*(;|$)/)) continue; //skip commented or blank lines
//                    if (all[fwdinx].match(/^[a-z0-9_]+\s*:/i)) continue; //skip labels
                    if (all[fwdinx].match(labeldef)) continue; //skip labels
                    if (!all[fwdinx].match(goaway)) break;
                    line = all[inx] = all[fwdinx].trimln() + ";;R4 " + line; //update "line" for further optimization
                    if (labels[parts[2]].numrefs == 1) all.comment(labels[parts[2]].definition, "U4"); //remove unneeded label
//no                    return; //look for other optimizations
                }
//remove useless jumps:
            if (parts = line.match(labeldef))
                if (all.lookback(new RegExp("^\\s+goto\\s+" + parts[1] + "(\\s|;|$)", "i"), inx, true))
                    all.comment(all.backinx, "G2");
        });
//one last pass:
//SWOPCODE is done here (all opcodes need to be in final position prior)
        lines.forEach((line, inx, all) =>
        {
//SWOPCODE: move opcodes
//	MOVLW	0x01	
//;; 	BANKSEL	_swopcode	
//	MOVWF	_swopcode	
            if (line.match(/^\s+movwf\s+_swopcode\s*$/i))
                if (all.lookback(/^\s+movlw\s+(0x[0-9a-f]+)\s*(;|$)/i, inx, true)) //CLRF would be useless (no need for SWOPCODE) so ignore it
                {
                    var mvline = null, adjust = parseInt(all.backparts[1].slice(2), 16), svadj = adjust;
                    all.comment(all.backinx, "P1");
                    all.comment(inx++, "P2");
                    for (;; ++inx) //look fwd
                    {
                        if (all[inx].match(/^\s*(;|$)/)) continue; //skip commented or blank lines
//                        if (all[inx].match(/^[a-z0-9_]+\s*:/i)) continue; //skip labels
                        if (all[inx].match(labeldef)) { console.log(";line-label"); continue; } //skip labels
                        if (!mvline) { mvline = inx; continue; } //remember instr to be moved
//swap moving line with current line:
                        var svline = all[mvline];
                        all[mvline] = all[inx].trimln() + `;;P3 move# ${adjust} was: ` + svline;
                        all[inx] = svline;
                        mvline = inx;
                        if (--adjust) continue;
                        all[inx] = `${all[inx].trimln()} ;;P4 moved down ${svadj} from above\n`;
                        break;
                    }
                }
        });
        return altered;
    }
};


//third clean-up text:
//cat build/%BASENAME%-fixup.asm | grep -v "^\s*;" | sed 's/;;.*$$//' > build\%BASENAME%.asm
function text_cleanup()
{
    return thru2(xform, flush); //{ objectMode: true, allowHalfOpen: false },

    function xform(chunk, enc, cb)
    {
        if (isNaN(++this.numlines)) this.numlines = 1;
        if (typeof chunk != "string") chunk = chunk.toString(); //TODO: enc?
//        chunk = chunk.replace(/[{,]\s*([a-z\d]+)\s*:/g, (match, val) => { return match[0] + '"' + val + '":'; }); //JSON fixup: numeric keys need to be quoted :(
        if (chunk.length)
            if (!chunk.match(/^\s*;/)) //non-blank line
                this.push(chunk.replace(/;;.*$/, "")) //remove commented out lines
        cb(); //null, chunk);
    }

    function flush(cb)
    {
        this.push(`;pass 3: #lines ${this.numlines || 0}\n`);
        cb();
    }
}


String.prototype.trimln = function() { return this.replace(/\n$/, ""); }

function percent(num, den)
{
    return Math.floor(10 * 100 * (num || 0) / (den || 1) + 0.5) / 10;
}


//EOF
