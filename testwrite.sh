#!/bin/sh

if [ "$1" ] ; then
    DIR=$1
else
    TMP=/tmp/disk-$$.img
    DIR=/tmp/mount-$$
    ./mktest $TMP
    mkdir -p $DIR
    ./homework -s -image $TMP $DIR
fi

#-------------------
sh -c "> $DIR/zz/foo" 2<&- &&     FAIL="$FAIL 1"
sh -c "> $DIR/file.A/foo" 2<&- && FAIL="$FAIL 2"
sh -c "> $DIR/dir1" 2<&- &&       FAIL="$FAIL 3"

> $DIR/file.x  || FAIL="$FAIL 4"
ls $DIR/file.x > /dev/null || FAIL="$FAIL 4"

> $DIR/dir1/file.x  || FAIL="$FAIL 5"
ls $DIR/dir1/file.x > /dev/null || FAIL="$FAIL 5"

mkdir $DIR/q
for i in $(seq 32); do
    > $DIR/q/f-$i || FAIL="FAIL 6-$i"
    [ -f $DIR/q/f-$i ] || FAIL="FAIL 6-$i"
done
rm -rf $DIR/q			# not enough i-nodes otherwise

if [ ! "$FAIL" ] ; then
    echo "MKNOD tests: PASSED"
else
    echo "MKNOD tests: FAILED (${FAIL# })"
fi
FAIL=

#-------------------

mkdir $DIR/zz/foo 2<&- &&     FAIL="$FAIL 1"
mkdir $DIR/file.A/foo 2<&- && FAIL="$FAIL 2"
mkdir $DIR/dir1 2<&- &&       FAIL="$FAIL 3"

mkdir $DIR/dir.x || FAIL="$FAIL 4"
ls -d $DIR/dir.x > /dev/null || FAIL="$FAIL 4"
[ -d $DIR/dir.x ] || FAIL="$FAIL 4"

mkdir $DIR/dir1/dir.x || FAIL="$FAIL 5"
ls -d $DIR/dir1/dir.x > /dev/null || FAIL="$FAIL 5"
[ -d $DIR/dir1/dir.x ] || FAIL="$FAIL 5"

mkdir $DIR/x || FAIL="$FAIL 6"
for i in $(seq 32); do
    mkdir $DIR/x/d-$i 2<&- || FAIL="$FAIL 6-$i"
    [ -d $DIR/x/d-$i ] || FAIL="$FAIL 6-$i"
done
rm -rf $DIR/x

if [ ! "$FAIL" ] ; then
    echo "MKDIR tests: PASSED"
else
    echo "MKDIR tests: FAILED (${FAIL# })"
fi
FAIL=

#-------------------

rm $DIR/zz/foo 2<&- && FAIL="$FAIL 1"
rm $DIR/file.A/foo 2<&- && FAIL="$FAIL 2"
rm $DIR/dir1 2<&- && FAIL="$FAIL 3"

while read f; do
    rm $DIR/$f || FAIL="$FAIL $f(a)"
    [ -f $DIR/$f ] && FAIL="$FAIL $f(b)"
done <<EOF
file.A
file.7
dir1/file.0
dir1/file.2
dir1/file.270
EOF

if [ ! "$FAIL" ] ; then
    echo "UNLINK tests: PASSED"
else
    echo "UNLINK tests: FAILED (${FAIL# })"
fi
FAIL=

#-------------------

rmdir $DIR/zz/foo 2<&- && FAIL="$FAIL 1"
rmdir $DIR/file.A/foo 2<&- && FAIL="$FAIL 2"
rmdir $DIR/file.x 2<&- && FAIL="$FAIL 3"
rmdir $DIR/dir1 2<&- &&   FAIL="$FAIL 4" # not empty

rmdir $DIR/dir1/dir.x 2<&- || FAIL="$FAIL 5"
[ -d $DIR/dir1/dir.x ] && FAIL="$FAIL 5"

rmdir $DIR/dir.x 2<&- || FAIL="$FAIL 6"
[ -d $DIR/dir.x ] && FAIL="$FAIL 6"

if [ ! "$FAIL" ] ; then
    echo "RMDIR tests: PASSED"
else
    echo "RMDIR tests: FAILED (${FAIL# })"
fi
FAIL=

for size in 100 1817 7201 269312; do
    ck="$(yes this-is-a-test | head -c $size | cksum)"
    mkdir $DIR/$size
    for b in 111 1701 4096; do
	fail=
	f=$DIR/$size/f-$b
	yes this-is-a-test | head -c $size | dd oflag=direct bs=$b of=$f 2<&-
	[ "$fail" ] && echo DD FAILED: disk full?
	[ -f $f ] || FAIL="$FAIL $size-$b"
	z="$(cat $DIR/$size/f-$b | cksum)"
	[ "$z" = "$ck" ] || FAIL="$FAIL $size-$b"
	[ "$z" = "$ck" ] || {
	    echo $f $z $ck
	    ls -l $f
	    }
    done
done
	
if [ ! "$FAIL" ] ; then
    echo "WRITE tests: PASSED"
else
    echo "WRITE tests: FAILED (${FAIL# })"
fi

[ "$FAIL" ] && exit

FAIL=

#-------------------

if [ ! "$1" ] ; then
    fusermount -u $DIR
    ./read-img $TMP | tail -2
    rm $TMP
    rmdir $DIR
fi

exit

fs_write:
• ignore path validation, since FUSE checks with fs_getattr first
• write with the same set of 4 different block sizes. (<1024, 1024, >1024, >2048)
• overwrite existing files with same set of block sizes, using the 'conv=notrunc' option to ‘dd’
• create small, medium, and large files
• run out of space on the disk. verify that the resulting file isn't "broken" after you do that – i.e.
you can read it, and it is as long as ‘ls -l’ says it is.


WRITE TESTING NOTE:
Always create a new disk image each time you run your program, unless you're *sure* that you didn't corrupt the image because of a bug in your program. Note that you can use the 'read-img' tool to print out the contents of a disk image in (somewhat) human-readable form.
 
fs_mknod, fs_mkdir: the cases are the same for both:
• bad path /a/b/c - b doesn't exist
• bad path /a/b/c - b isn't directory
• bad path /a/b/c - c exists, is file
• bad path /a/b/c - c exists, is directory
• good path – verify successful completion
• good path, but fail due to full directory
See the post at the beginning of the thread for how to create a 0-length file.
 
fs_unlink:
• bad path /a/b/c - b doesn't exist
• bad path /a/b/c - b isn't directory
• bad path /a/b/c - c doesn't exist
• bad path /a/b/c - c is directory
• /a/b/c - actually deletes 'c'
￼￼
Remove zero-length, small (<6K), medium (<262K), large (>262K) files.
 
Use read-img afterwards to verify that all blocks were freed. In particular, the last 2 lines of output will only contain 4 words (“unreadable inodes:” and “unreachable blocks:”) if there are no lost blocks, so you can script a test like this:
test "$(./read-img foo.img | tail -2 | wc –-words)" = 4 || fail Test 1 failed
fs_truncate: check that 'truncate 0' leaves a 0-length file. Test on zero-length, small, medium, and large files; check that there are no lost blocks. (you should have factored out the common code between fs_unlink and fs_truncate, so that if one works the other works)
 
fs_rmdir: very similar to fs_unlink.
• bad path /a/b/c - b doesn't exist
• bad path /a/b/c - b isn't directory
• bad path /a/b/c - c doesn't exist
• bad path /a/b/c - c is file
• directory not empty
• /a/b/c - check that it actually removes 'c'
 
fs_write:
• ignore path validation, since FUSE checks with fs_getattr first
• write with the same set of 4 different block sizes. (<1024, 1024, >1024, >2048)
• overwrite existing files with same set of block sizes, using the 'conv=notrunc' option to ‘dd’
• create small, medium, and large files
• run out of space on the disk. verify that the resulting file isn't "broken" after you do that – i.e.
you can read it, and it is as long as ‘ls -l’ says it is.
 
Note that with a repeatable test script the file should always be the same size, so you can do something like this (change ‘2048’ as necessary):
test $(wc –-bytes dir/file) = 2048 || fail Test 1
fs_chmod, fs_utime: test that they actually change the values:
chmod 754 dir/file.A
test “$(ls -l dir/file.A)” =  ‘-rwxr-xr-- 1 student student 1000 Jul 13 2012 dir/file.A’
touch -d ‘Jan 01 2000’ dir/file.A
test “$(ls -l dir/file.A)” = ‘-rwxr-xr-- 1 student student 1000 Jan 1 2000 dir/file.A’
fs_rename: This has a bunch of cases. Remember that we cheat in two cases – (a) no moving files across directories, and no replacing the destination file.
• mv /a /b, mv /d/a /d/b - ‘a’ doesn’t exist
• mv /d/a /d/b - ‘d’ is a file
• mv /a /b, mv /d/a /d/b - ‘b’ exists and is a file
• mv /a /b, mv /d/a /d/b - ‘b’ exists and is a directory
• mv /d1/a /d2/b – no moving between directories
• mv /a /b, mv /d/a /d/b - success
