#!/bin/sh
echo "Filename prefix for output files ?"
read OUTNAME
echo "ROM file size in bytes [16384/32768] ?"
read SIZE_RAW

#SIZE_RAW=32768
SIZE=`echo "$SIZE_RAW-1024" |bc`
BANK=0
OUTFILE=0
FILES_LEFT=`ls $HOME/JUKEBOX_ROM_CREATOR/3-topack/*.bin 2>/dev/null |wc -l`


while [ $FILES_LEFT -gt 0 ]
do
FILE=30
WD="$HOME/JUKEBOX_ROM_CREATOR/3-topack/"
TD="$HOME/JUKEBOX_ROM_CREATOR/4-packed/"
echo "Starting loop $OUTFILE"

#copy loader
cp $HOME/JUKEBOX_ROM_CREATOR/loader_dummy.bin $TD/BANK$BANK.bin

cd $WD
#restore files
rename 's/\_+//g' *

SEQ=1
for f in `ls -S $WD|egrep -v "_|.pat"`; do
	echo analyzing: $f
	FILESIZE=`stat --printf="%s" $f`
	echo $S
	#would it fit?
	PACKED=`stat --printf="%s" $TD/BANK$BANK.bin`
	TOTEST=`echo "$PACKED+$FILESIZE" |bc`
	if [ "$TOTEST" -le "$SIZE" ]
		then
		echo "PRG#"$SEQ" "$f FITS
		echo $SEQ" "$f|sed 's/-//g'|sed 's/.bin//g' >> $TD/$OUTNAME$OUTFILE.txt
		cat $f $TD/BANK$BANK.bin > /tmp/BANK$BANK.bin
		mv /tmp/BANK$BANK.bin $TD/BANK$BANK.bin

		#move PAT file as well into dest dir
		PATFILE=`echo "${f%.*}"`
		cp $PATFILE.pat $TD/$FILE-BANK$BANK-$PATFILE.pat
		FILE=`echo "$FILE-1" |bc`

		#delete files already packed
		rm $f 2>/dev/null
		rm $PATFILE 2>/dev/null
		
		#increase counter for packed files
		SEQ=`echo "$SEQ+1" |bc`

		else
		mv $f _$f
	fi
	if [ $SEQ -gt 17 ]
		then
			echo "PAT full, finalizing..."
			break
	fi
done

#normalize eprom size, first pad from end of programs to PAT area
FILESIZE=`stat --printf="%s" $TD/BANK$BANK.bin`
FILESIZE_CLEAN=`echo "$FILESIZE-1024" |bc`
PADDING=`echo "$SIZE_RAW-$FILESIZE" |bc`
head -c $FILESIZE_CLEAN $TD/BANK$BANK.bin > $TD/BANK$BANK.tmp
dd if=/dev/zero bs=1 count=$PADDING | tr '\000' '\377' >> $TD/BANK$BANK.tmp
#now add PAT files and pad up to the beginning of loader space
cat $TD/BANK$BANK.tmp $TD/BANK0{0..40}*.pat > $TD/BANK$BANK.bin 2>/dev/null
cat $TD*.pat >> $TD/BANK$BANK.bin
FILESIZE=`stat --printf="%s" $TD/BANK$BANK.bin`
PADDING=`echo "$SIZE_RAW-$FILESIZE-768" |bc`
#fill with FF
dd if=/dev/zero bs=1 count=$PADDING | tr '\000' '\377' >> $TD/BANK$BANK.bin
#copy loader (must be padded to 768 bytes)
cat $HOME/JUKEBOX_ROM_CREATOR/loader_real.bin >> $TD/BANK$BANK.bin
rm $TD/*.tmp

$HOME/JUKEBOX_ROM_CREATOR/patter.sh

echo "Loop $OUTFILE completed."

#create output file
cp $TD/BANK0.bin $TD/$OUTNAME$OUTFILE.BIN
#increment OUTFILE number
OUTFILE=`echo "$OUTFILE+1" | bc`
#update number of files left
FILES_LEFT=`ls $HOME/JUKEBOX_ROM_CREATOR/3-topack/*.bin 2>/dev/null |wc -l`
sleep 1
rm $TD/BANK0.bin 2>/dev/null
done
rm $WD/*.pat 2>/dev/null
echo "All completed."
exit 0
