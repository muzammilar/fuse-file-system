#!/bin/sh

if [ "$1" ] ; then
    DIR=$1
else
    TMP=/tmp/disk-$$.img
    DIR=/tmp/mount-$$

    ./mktest $TMP
    mkdir -p $DIR
    ./homework -image $TMP $DIR
fi

#-------------------

ls -d $DIR/file.A || FAIL="$FAIL 1"
ls -d $DIR/file.7 || FAIL="$FAIL 2"
ls -d $DIR/dir1 ||   FAIL="$FAIL 3"

ls -d $DIR/dir1/file.2   || FAIL="$FAIL 4"
ls -d $DIR/dir1/file.270 || FAIL="$FAIL 5"

ls -d $DIR/file.x 2<&- &&        FAIL="$FAIL 6"
ls -d $DIR/not/file.A 2<&- &&    FAIL="$FAIL 7"
ls -d $DIR/not/file.2 2<&- &&    FAIL="$FAIL 8"
ls -d $DIR/file.A/file.2 2<&- && FAIL="$FAIL 9"
ls -d $DIR/file.A/file.7 2<&- && FAIL="$FAIL 10"

if [ ! "$FAIL" ] ; then
    echo "GETATTR tests: PASSED"
else
    echo "GETATTR tests: FAILED (${FAIL# })"
fi
FAIL=

#-------------------

[ "$(ls -C $DIR)" = 'dir1  file.7  file.A' ] || FAIL="$FAIL 1" 
if [ ! "$(ls -C $DIR)" = 'dir1  file.7  file.A' ] ; then
    echo LS1 FAIL:
    ls -C $DIR
fi
[ "$(ls -C $DIR/dir1)" = 'file.0	file.2	file.270' ] || FAIL="$FAIL 2"

if [ ! "$FAIL" ] ; then
    echo "READDIR tests: PASSED"
else
    echo "READDIR tests: FAILED (${FAIL# })"
fi
FAIL=
 
#-------------------

while read file cksm; do
    for n in 111 1024 1517 2701; do
	val="$(dd iflag=direct if=$DIR/$file bs=$n 2<&- | cksum)"
	[ "$val" = "$cksm" ] || FAIL="$FAIL $n/$file"
    done
done <<EOF
file.7 94780536 6644
file.A 3509208153 1000
dir1/file.2 3106598394 2012
dir1/file.270 1733278825 276177
dir1/file.0 4294967295 0
EOF

if [ ! "$FAIL" ] ; then
    echo "READ tests: PASSED"
else
    echo "READ tests: FAILED (${FAIL# })"
fi
FAIL=

if [ ! "$1" ] ; then
    fusermount -u $DIR
    rmdir $DIR
    rm $TMP
fi
