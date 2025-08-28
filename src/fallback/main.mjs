import { exit } from "node:process";
import { basename } from "node:path";
import console from "node:console";
import { readdirSync } from "node:fs"
import { ProcessRequirements } from "./shared.mjs";
class PreprocessorPattern {
  regexp;
  preprocessor;
  constructor(regexp, preprocessor){
    this.regexp = regexp;
    this.preprocessor = preprocessor;
  }
}
/**@type {ProcessRequirements[]}*/
var allReqs = [];
/**@type {PreprocessorPattern[]}*/
var startPatterns = [],
/**@type {PreprocessorPattern[]}*/
  endPatterns = [],
  startPrep = "/*_START_DEV_*/",
  endPrep = "/*_END_DEV_*/"
/**
 * @type {RegExp}
 * */
var avoidRegex;

var mode = "files"
export default function(argv) {
  
  var setup = {
    main() {
      console.time("setup function");
      var argI = 0;
      do {
        const argument = argv[argI];
        if (argument.startsWith("--outdir=")) setup.build.setOutDir();
        else if (argument.startsWith("--files:")) break;
        else if (argument.startsWith("--dirs:")) { mode = "dirs"; break; }
        else if (argument.startsWith("--pattern-s=")) setup.build.addPreprocessorPattern(argument, startPatterns);
        else if (argument.startsWith("--pattern-e=")) setup.build.addPreprocessorPattern(argument, endPatterns);
        else if (argument.startsWith("--default-s=")) setup.build.setDefaultPreprocessor(argument, 's');
        else if (argument.startsWith("--default-e=")) setup.build.setDefaultPreprocessor(argument, 'e');
        else if (argument.startsWith("--avoid=")) setup.build.setAvoidRegex(argument);
        else {
          console.error("Option \"" + argument + "\" can't be used OR can't be used BEFORE --files: or --dirs:");
          exit(1);
        }
      } while (++argI < argv.length);
      if (argI >= argv.length - 1) {
        console.error(
          "You have to include a flag signifying the beginning of files/dirs (--files: / --dirs:) AND the files/dirs"
        );
        exit(1);
      }
      if (mode == "--dirs:") while (++argI < argv.length) setup.fill_reqs.fromDir(argv[argI]);
      else while (++argI < argv.length) setup.fill_reqs.fromFiles(argv[argI]);
      console.timeEnd("setup function");
    },
    build: {

      setOutDir(
      /**
      * @type {string}
      */argument) {
        // --outrdir=     10 
        if (argument.length == 10) {
          console.error("Argument \"" + argument + "\" doesn't provide any value.\n");
          exit(1);
        }
        outdir = argument.slice(10);
        if (argument[argument.length - 1] !== '/') outdir += '/';
      },
      setDefaultPreprocessor(argument, type) {
        if (argument.length == 13) {
          console.error("argument " + argument + " has no value\n");
          exit(1);
        }
        if (type == 's') {
          startPrep = argument.slice(13);
        } else {
          endPrep = argument.slice(13);
        }
      },
      setAvoidRegex(
        /**
         * @type {string}
         * */
        argument) {
        if (argument.length === 7) {
          console.error("argument --avoid has no value provided");
          exit(1);
        }
        avoidRegex = new RegExp(argument.slice(7));
      },
      addPreprocessorPattern(argument, /**@type {PreprocessorPattern[]}*/patterns) {
        if (argument.length == 13) {
          console.error("argument " + argument + " has no value\n")
          exit(1);
        }
        if (argument[13] != '/'
          || argument[14] == '/') {
          console.error("Argument's \"" + argument + "\" regex part doesn't start from slash OR has 2 shashe in a row");
          exit(1);
        }
        /**@type {string}*/
        var argumentAdjacentCopy = argument.slice(13);
        var endRegexI = argumentAdjacentCopy.search("/:");
        if (endRegexI == -1) {
          console.error("Argument's \"" + argument + "\" regex part deson't end with /: symbols\n");
          exit(1);
        }
        var regex = new RegExp(argumentAdjacentCopy.slice(0, endRegexI));
        argumentAdjacentCopy = argumentAdjacentCopy.slice(endRegexI + 2);
        if (!argumentAdjacentCopy.length) {
          console.error("Argument \"" + argument + "\" doesn't provide any pattern-text")
          exit(1);
        }
        patterns.push(new PreprocessorPattern(regex, argumentAdjacentCopy));
      }
    },
    fill_reqs: {
      fromFiles(/**@type {string}*/argument) {
        var in_out_separator = argument.search('|');
        if (
          argument[0] != '<' || argument[argument.length - 1] != '>'
          || in_out_separator == 1 || in_out_separator == argument.length
        ) {
          console.error("Argument \"" + argument + "\" is of wrong format")
          exit(1);
        }
        var userGaveOutput = in_out_separator != -1;
        var inputString = argument.slice(1, userGaveOutput ? in_out_separator - 1 : argument.length - 2);
        if (avoidRegex && avoidRegex.test(inputString)) return;
        const startPreprocessor = setup.fill_reqs.findPreprocessorUsingPattern(inputString, startPatterns) || startPrep;
        const endPreprocessor = setup.fill_reqs.findPreprocessorUsingPattern(inputString, endPatterns) || endPrep;
        var outputString = userGaveOutput
          ? argument.slice(in_out_separator + 1, argument.length - in_out_separator - 2)
          : outdir + basename(inputString);
        allReqs.push(new ProcessRequirements(inputString, outputString, startPreprocessor, endPreprocessor));
      },
      /**
       *@returns {string | null} preprocessor if found or null
       * */
      findPreprocessorUsingPattern(filename, /**@type {PreprocessorPattern[]}*/patterns) {
        for (var pattern of patterns)
          if (pattern.regexp.test(filename)) return pattern.preprocessor;
        return null;
      },
      iterateDir(dirString, skipDirStringChars, outDirString) {
        for (var entry of readdirSync(dirString, { withFileTypes: true })) {
          if (avoidRegex && avoidRegex.test(entry.name)) continue;
          if (entry.isDirectory())
            setup.fill_reqs.iterateDir(entry.name + '/', skipDirStringChars, outDirString)
          else {

          }
        }
      }
    }
  }

  if (argv.length == 0) {
    console.info(`
        This is an executable for removing unwanted code from production.
Just like macro in C++, but only "#if 0". 
In your target file you mark some part with a "starting phrase" and "ending phrase",
        which you can manually specify.This exe removes these phrases and everything in between.
Use it for dynamic swagger specifications, logs to console, debugging imports and more.
1) Arguments
    1.1) Order: setting flags -> flag singnifying targets -> targets;
    !setting flags are optional
    1.2) Setting flags:

    1.2.1)--outdir=PATH
  All targets with unspecified output next to them will use PATH for outputs.
  PATH can be absolute and relative.
        Default - ./ (current working directory)
    Example: --outdir=../almost-dist

    1.2.2)--avoid=REGEXP
  If some targets match the REGEXP(don't give slashes) - they 
  won't be processed
  Example: --avoid=.git |.dockerignore |.conf
  
  1.2.3)--default -s=START_PHRASE
  IF you don't give it, /*_START_DEV_*/ will be used
  Example for python target: "--default-s=#START DEV"

    1.2.4)--default -e=END_PHRASE
  This exe used /*_END_DEV_*/ by its default
  Example for lua: "--default-e=--finish of development"

    1.2.5)--pattern - s=/FILE_REGEXP/: START_PHRASE
  If target matches FILE_REGEXP between slashes, then it will use 
  START_PHRASE instead of the default one.
  This option can be used several times.
        Example: --pattern - s=/.py/: #START_DEV

    1.2.6)--pattern - e=/FILE_REGEXP/: END_PHRASE
    Same as for start phrase.
        Example: "--pattern-e=/.ts/:/*My own end*/"

    1.3) flags before targets AND targets
    1.3.1)--files:
  This means that from now on you will give ONLY files to process.
  Target files(relative or absolute) must be passed between "<>" signs.

        Example 1: --outdir=./almost-dist --files: <input.txt>
  In this example "input.txt" will become "almost-dist/input.txt"
  If you don't give --outdir, this example would try to create 
  another input.txt in ./ directory, resulting in an error.
  
  But you can give it your own output name using | sign
  Example 2: --files: "<input.txt|output.txt>"
  This overrides--outdir

  You can feed this exe with many files
  Example 3: --outdir=./dist --files: <one.txt> <D:/ / two.txt > <three.txt| D://out.txt> 

    1.3.2)--dirs:
  This instructs exe to recursively process directories.
  Format and use cases are the same as of files.

        Example 1: --dirs: <input-dir> <dir2|outdir>

        2) For full examples go to github page)
`);
    exit(0);
  } else if (argv.length == 1) {
    console.error("You should pass at least 2 arguments");
  }

  setup.main();
}


