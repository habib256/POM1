#!/bin/sh

WD="$HOME/JUKEBOX_ROM_CREATOR/4-packed/"
TD="$HOME/JUKEBOX_ROM_CREATOR/final/"
FN="$WD/BANK0.bin"

#replace byte function: 1st argument=offset (decimal), 2nd argument=hex value to write
replaceByte(){
	PAT_NEXT=`echo "$START_PAT+15" | bc`
	HE_NEXT=`xxd -p -l1 -s $PAT_NEXT $FN |tr a-f A-F`
	if [ "$HE_NEXT" != "FF" ] || [ "$LOOP" = 0 ]
	then
		echo $2 | ascii2binary -b h | dd of="$FN" bs=1 seek=$1 count=1 conv=notrunc 2>/dev/null
		sync
	fi
}

START_EPROM_LO=00
EPROM_SIZE=`stat --printf="%s" $FN`
if [ "$EPROM_SIZE" -eq 16384 ]
then
	START_EPROM_HI=80
	START_PAT=15360
	echo -n "16"
else
	START_EPROM_HI=40
	echo -n "32"
	START_PAT=31744
fi
echo " kB EPROM detected: $EPROM_SIZE bytes"

#starting parameter
PRG_LO=$START_EPROM_LO
PRG_HI=$START_EPROM_HI

LOOP=0
while [ "$HE" != "FF" ]
do
	#read header in order to understand if there is a BASIC program or not
	HE=`xxd -p -l1 -s $START_PAT $FN |tr a-f A-F`
	echo -n "HEADER="$HE
        if [ "$HE" = "FF" ]
        then
                break
                echo ending
        fi
	if [ "$HE" = "F1" ]
	then
		echo -n " -- BASIC  PROGRAM DETECTED"
		TAIL=B6	#byte to add at the end of the program because of the presence of the pointers
	else
		echo -n " -- BINARY PROGRAM DETECTED"
		TAIL=0
	fi
	#write very first start address, which is at the beginning of the eprom file
	PAT_LO=`echo "$START_PAT+9" | bc`
	PAT_HI=`echo "$START_PAT+10" | bc` #next byte, but still in decimal
	replaceByte $PAT_LO $PRG_LO
	replaceByte $PAT_HI $PRG_HI
	P=$PRG_HI$PRG_LO
	#read length of the program: pointers
	LEN_LO=`echo "$START_PAT+13"|bc`
	LEN_HI=`echo "$START_PAT+14"|bc`
	#values
	LL=`xxd -p -l1 -s $LEN_LO $FN |tr a-f A-F`
	LH=`xxd -p -l1 -s $LEN_HI $FN |tr a-f A-F`
	L=$LH$LL
	echo -n "  -- PROGRAM LENGTH="$L


	#ADD TO PREVIOUS ADDRESS
	N=`echo "obase=16; ibase=16; ($P+$L+$TAIL)" | bc`
	echo -n " -- NEXT START ADDRESS="$N
	PRG_LO=`echo $N|cut -b 3-4`
	PRG_HI=`echo $N|cut -b 1-2`
	#echo "Pointers HH LL="$PRG_HI $PRG_LO

	#WRITE NEXT START TO THE NEXT PAT
	NEW_LO=`echo "$START_PAT+24"|bc`
	NEW_HI=`echo "$START_PAT+25"|bc`
	replaceByte $NEW_LO $PRG_LO
       	replaceByte $NEW_HI $PRG_HI
	#prepare next iteration
	START_PAT=`echo "$START_PAT+15" | bc`
	echo -n " -- Next PAT="$START_PAT
	echo
	LOOP=`echo "$LOOP+1" | bc`
done
echo
echo
if [ "$EPROM_SIZE" -eq 32768 ]
	then
	echo "32kB file detected. Flipping banks..."
	head -c 16384 $FN > lo.bin
	tail -c 16384 $FN > hi.bin
	mv $FN $FN.bak
	cat hi.bin lo.bin > $FN
	rm lo.bin
	rm hi.bin
fi
rm $WD*.pat
rm $WD*.bak 2>/dev/null
echo "PAT Completed."

exit 0
