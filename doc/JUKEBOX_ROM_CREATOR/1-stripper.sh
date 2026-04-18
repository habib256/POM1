#!/bin/sh

WD="$HOME/JUKEBOX_ROM_CREATOR/1-programs/"
TD="$HOME/JUKEBOX_ROM_CREATOR/2-stripped/"
#STRIP_BYTES=0
#HEADER=FE

cd $WD
for f in $WD*; do
	echo "found:"

	#FILENAME
	FILENAME_ORIG=`echo $f| rev | cut -d"/" -f1 | rev`
	echo $FILENAME_ORIG
	A=`echo $FILENAME_ORIG|cut -b 1-7`
	B=`echo -e $FILENAME_ORIG|rev | cut -b 8 | rev`
	FILENAME_NEW=$A$B
	#check for short names, replace empty spaces with "-"
	SHORT=`echo $FILENAME_NEW| grep -b -o "#" | awk 'BEGIN {FS=":"}{print $1}'`
	C=`echo $FILENAME_NEW|cut -b 1-$SHORT`
	FILENAME_NEW=`echo $C"-------" |cut -b 1-8`
	echo -n $FILENAME_NEW

	#ISBASIC
	BAS=`echo $f|grep "#f1"|grep -c grep -v`
	if [ "$BAS" -eq "1" ]
	then 
	STRIP_BYTES=512
	HEADER=F1
	else
	STRIP_BYTES=0
	HEADER=FE
	fi

	#LENGTH
	L=`stat --printf="%s" $FILENAME_ORIG`
	LENGTH_DEC=`echo "$L-$STRIP_BYTES" |bc`
	LENGTH_HEX=`printf '%04x\n' $LENGTH_DEC|tr a-f A-F`
	LEN_HI=`echo $LENGTH_HEX|cut -b 1-2|tr a-f A-F`
	LEN_LO=`echo $LENGTH_HEX|cut -b 3-4|tr a-f A-F`

	#START
    	START_HEX=`echo -e $FILENAME_ORIG|rev | cut -b -4|rev|tr a-f A-F`
	START_HI=`echo $START_HEX|cut -b 1-2`
	START_LO=`echo $START_HEX|cut -b 3-4`

	#SUMMARY
	echo " BAS="$BAS" LEN="$LENGTH_DEC" LEN_HEX="$LENGTH_HEX" LEN_HI="$LEN_HI" LEN_LO="$LEN_LO" START_HEX="$START_HEX" START_HI="$START_HI" START_LO="$START_LO

	#CREATE PAT and remove "-"
	echo "$HEADER" | ascii2binary -b h > $TD/$FILENAME_NEW.pat
	echo -n "$FILENAME_NEW"|sed 's/-/\ /g' >> $TD/$FILENAME_NEW.pat
	echo FF FF $START_LO $START_HI $LEN_LO $LEN_HI | ascii2binary -b h >> $TD/$FILENAME_NEW.pat

	#CREATE POINTERS FILE (BASIC ONLY)
	if [ "$BAS" -eq "1" ]
        then
		dd if="$FILENAME_ORIG" of="$TD/$FILENAME_NEW".poi bs=1 count=182 skip=74 2>/dev/null
		dd if="$FILENAME_ORIG" of="$TD/$FILENAME_NEW".prg bs=1 skip=512 2>/dev/null
		#cat "$TD/$FILENAME_NEW".prg "$TD/$FILENAME_NEW".poi > "$TD/$FILENAME_NEW".bin
		mv "$TD/$FILENAME_NEW".prg "$TD/$FILENAME_NEW".bin

		#slicer to extract only the program, not the variables part (Added 19/05/2020)
		REAL_BASIC_START_HI=`xxd -p -seek 129 -l 1 "$TD/$FILENAME_NEW".poi|tr a-f A-F`
		REAL_BASIC_START_LO=`xxd -p -seek 128 -l 1 "$TD/$FILENAME_NEW".poi|tr a-f A-F`
		REAL_BASIC_START=$REAL_BASIC_START_HI$REAL_BASIC_START_LO
		echo -n "Slicing: REAL_BASIC_START="$REAL_BASIC_START
		BYTES_TO_SLICE=`echo "obase=10; ibase=16; (($REAL_BASIC_START)-($START_HEX)+1)" | bc`
		echo " BYTES TO SLICE="$BYTES_TO_SLICE
		tail -c +$BYTES_TO_SLICE  "$TD/$FILENAME_NEW".bin > "$TD/$FILENAME_NEW".slc
		cat "$TD/$FILENAME_NEW".slc "$TD/$FILENAME_NEW".poi > "$TD/$FILENAME_NEW".bin

		#calculating new length from .slc file (.bin already has pointers in tail)
		NL=`stat --printf="%s" "$TD/$FILENAME_NEW".slc`
	        NLENGTH_HEX=`printf '%04x\n' $NL|tr a-f A-F`
	        NLEN_HI=`echo $NLENGTH_HEX|cut -b 1-2|tr a-f A-F`
	        NLEN_LO=`echo $NLENGTH_HEX|cut -b 3-4|tr a-f A-F`
		echo "         NEW_LEN="$NL" NEW_LEN_HEX="$NLENGTH_HEX" NEW_LEN_HI="$NLEN_HI" NEW_LEN_LO="$NLEN_LO


		#rewriting PAT with new start and new length
		echo "$HEADER" | ascii2binary -b h > $TD/$FILENAME_NEW.pat
	        echo -n "$FILENAME_NEW"|sed 's/-/\ /g' >> $TD/$FILENAME_NEW.pat
	        echo FF FF $REAL_BASIC_START_LO $REAL_BASIC_START_HI $NLEN_LO $NLEN_HI | ascii2binary -b h >> $TD/$FILENAME_NEW.pat

	else
		cp "$FILENAME_ORIG" "$TD/$FILENAME_NEW".bin
        fi




done
rm $TD/*.prg 2>/dev/null
rm $TD/*.poi 2>/dev/null
rm $TD/*.slc 2>/dev/null
echo "Strip completed."


exit 0
