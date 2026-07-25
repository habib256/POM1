// Apple 1
.file [name="%O", type="prg", segments="Program"]
.segmentdef Program [segments="Code, Data"]
.segmentdef Code [start=$4000,min=$4000,max=$7fff,outBin="apple1_codeseg.bin"]
.segmentdef Data [start=$0280,min=$0280,max=$0fff,outBin="apple1_dataseg.bin"]
