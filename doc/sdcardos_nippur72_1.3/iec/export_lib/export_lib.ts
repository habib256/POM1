import fs from "fs";
import path from "path";

type Symbols = {[key:string]:number};

function extract_dasm_info(rootname: string) {
   const dasm_info = {        
      symbols: get_symbols(rootname),
      code: get_binary_file(rootname)
   }
   return dasm_info;
}

function get_symbols(rootname: string) {
   const symbols = fs.readFileSync(`${rootname}.sym`).toString();

   const regex = /(?<symbol>[^\s]*)\s*(?<address>[0-9a-fA-F]{4})/gm;

   const symbol_table: {[key:string]: number} = {};

   while(true) {
      let match = regex.exec(symbols);   
      if(match === null || match.groups === undefined) break;
      symbol_table[match.groups.symbol] = Number.parseInt(match.groups["address"], 16);        
   }

   return symbol_table;
}

function get_binary_file(rootname: string) {
   const bin_file = fs.readFileSync(`${rootname}.bin`);    
   return Array.from(new Uint8Array(bin_file));    
}

function export_symbol_function(name:string, sym: Symbols) {
   let address = sym[name];   
   return `func_ptr _${name}_ = (func_ptr) 0x${address.toString(16)};\r\n`+
          `#define ${name} (*_${name}_)\r\n`;
}

function export_symbol_byte(name:string, sym: Symbols) {
   let address = sym[name];   
   return `byte *const ${name} = (byte *) 0x${address.toString(16)};\r\n`;
}

function export_symbol_word(name:string, sym: Symbols) {
   let address = sym[name];   
   return `word *const ${name} = (word *) 0x${address.toString(16)};\r\n`;
}

function export_lib(rootname: string, outname: string, varname: string) {
   const ob = extract_dasm_info(rootname);

   let code_string = ob.code.map((e,i)=>{
      let cr = i % 16 == 0 ? "\r\n   " : "";
      return `${cr}${e}`;
   });

   let exported: string[] = [];

   exported.push(export_symbol_function("INITPIA", ob.symbols));
   exported.push(export_symbol_function("CALCFILENAMELENGTH", ob.symbols));
   exported.push(export_symbol_function("SAVE", ob.symbols));
   exported.push(export_symbol_function("LOAD", ob.symbols));
   exported.push(export_symbol_function("DIR", ob.symbols));
   exported.push(export_symbol_function("IECERR", ob.symbols));
   exported.push(export_symbol_function("IECCMD", ob.symbols));
   exported.push(export_symbol_function("INTBASLOAD", ob.symbols));
   exported.push(export_symbol_function("PRINT_ERR_NBF", ob.symbols));
   exported.push(export_symbol_function("INTBASSAVE", ob.symbols));
   exported.push(export_symbol_function("PRNINT", ob.symbols));   
   exported.push(export_symbol_function("DEVICE_NP", ob.symbols));

   exported.push(export_symbol_byte("FA",     ob.symbols));
   exported.push(export_symbol_byte("SA",     ob.symbols));
   exported.push(export_symbol_byte("VERCK",  ob.symbols));
   exported.push(export_symbol_byte("STATUS", ob.symbols));
   exported.push(export_symbol_byte("FNLEN",  ob.symbols));

   exported.push(export_symbol_word("FNADR", ob.symbols));
   exported.push(export_symbol_word("SAL", ob.symbols));
   exported.push(export_symbol_word("EAL", ob.symbols));
   //exported.push(export_symbol_word("MEMUSS", ob.symbols));

   let main_symbol = ob.symbols["MAIN"];

   // __address(0x....) __export byte iec_code[] = { 1,2,3 };
   const file_content = 
      `// file generated automatically, do not edit\r\n\r\n`+
      `typedef void (*func_ptr)(void);\r\n\r\n`+
      `${exported.join("\r\n")}\r\n`+
      `__address(${main_symbol}) __export byte iec_code[] = { ${code_string.join(",")} };`;

   fs.writeFileSync(outname, file_content );
}

const source_folder = path.resolve(__dirname, "../out/iec_lib");
const dest_fname = path.resolve(__dirname, "../../sdcard/iec_lib_c_import.h");
const varname = "iec_lib_info";

export_lib(source_folder, dest_fname, varname);

console.log(`created: ${dest_fname}`);
