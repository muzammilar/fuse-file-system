

#To calculate the total number of blocks read:

grep read /tmp/fs-xxxx.log | awk '{sum = sum+$3}END{print sum}'

#Similarly for write. ('awk' is a horrible language. Code in '{}' is executed once per input line; 'END{}' is executed at the end.)

#If you were just counting how many lines had 'read' you could use wc (wordcount):

grep read /tmp/fs-xxxx.log | wc