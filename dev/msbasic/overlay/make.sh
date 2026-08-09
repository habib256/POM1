if [ ! -d tmp ]; then
	mkdir tmp
fi

for i in cz6502 ; do

echo $i
ca65 -D $i msbasic.s -o tmp/$i.o -l tmp/$i.lst &&
ld65 -C $i.cfg tmp/$i.o -o tmp/$i.bin -Ln tmp/$i.lbl

done

