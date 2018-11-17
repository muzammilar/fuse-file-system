CS 7600: Intensive Operating Systems
====================================

FUSE File System
----------------

\#\#\#\# Muzammil Abdul Rehman

**Notes**: \* The main code of the assignment was modified in `homework.c` file.
\* `test-results.txt` file contains the disk blocks read/written for all the
individual parts of the assignment for *50 MB*,*150 MB* and *500 MB* image
sizes. The section `Without specifying any part` in the file refers to the
condition when the *file system* was run without specifying any parts
explicitly. This inherently referred to `Part 2` of this assignment, since most
of the conditions were `if homework_part == 1` or `if homework_part > 2` or `if
homework_part > 3`. \* `test-results.pdf` file contains the disk blocks
read/written as well as the number of disk operations performed and a small
discussion section on the cache mechanisms. \* `testread_testwrite.txt` file
contains the output of `testread.sh` and `testwrite.sh`. There's one minor bug
in the write function(which is sometimes reproducible) as seen in the file. Due
to this bug sometimes 1 and sometimes 2 write tests fail.
