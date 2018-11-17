


def get_stats(file_type, file_name):
    reads_ops = 0;
    reads_len = 0;
    writes_ops = 0;
    writes_len = 0;
    last_seen = ""
    last_seen_number = -1
    with open(file_name,'rB') as f:
        for line in f:
            x = line.split(" ")
            if x[0] == "read":
                try:
                    if last_seen == "read":
                        if int(x[1]) == last_seen_number:
                            reads_ops += 0;
                        else:
                            reads_ops += 1;
                    else:
                        last_seen = "read"
                    last_seen_number = int(x[1]) + int(x[2])
                    reads_len += int(x[2]);
                except:
                    print line
            if x[0] == "write":
                try:
                    if last_seen == "write":
                        if int(x[1]) == last_seen_number:
                            writes_ops += 0;
                        else:
                            writes_ops += 1;
                    else:
                        last_seen = "write"
                    last_seen_number = int(x[1]) + int(x[2])
                    writes_len += int(x[2]);
                except:
                    print line

    print file_type
    print "No. of read operations: ", reads_ops
    print "No. of read blocks: ", reads_len
    print "No. of write operations: ", writes_ops
    print "No. of write blocks: ", writes_len
    print "\n"



def main():
    file_types = ['50MB defualt','50MB part 1','50MB part 2', '50MB part 3','50MB part 4', '150MB defualt','150MB part 1','150MB part 2', '150MB part 3','150MB part 4', '500MB defualt','500MB part 1','500MB part 2', '500MB part 3','500MB part 4',]

    # @ vik add the file names here in this dict. havent tested the function yet.
    file_names_dict = {'50MB defualt': 'fs-2616.log',
                       '50MB part 1': 'fs-3067.log',
                       '50MB part 2': 'fs-3465.log',
                       '50MB part 3': 'fs-3864.log',
                       '50MB part 4': 'fs-4265.log',
                       '150MB defualt': 'fs-4671.log',
                       '150MB part 1': 'fs-4921.log',
                       '150MB part 2': 'fs-5335.log',
                       '150MB part 3': 'fs-5731.log',
                       '150MB part 4': 'fs-6133.log',
                       '500MB defualt': 'fs-6637.log',
                       '500MB part 1': 'fs-6929.log',
                       '500MB part 2': 'fs-7203.log',
                       '500MB part 3': 'fs-7472.log',
                       '500MB part 4': 'fs-7743.log'}
    for fn in file_types:
        get_stats(fn, file_names_dict[fn])

main()
