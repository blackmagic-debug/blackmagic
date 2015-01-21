#!/usr/bin/perl
#
# Convert the output of objdump to an array of bytes are can include
# into our program.

while (<>) {
    if (m/^\s*([0-9a-fA-F]+):\s*([0-9a-fA-F]+)(.*)/) {
	my $addr = "0x$1";
	my $value = $2;
	if (length ($value) == 4) {
	    print "  [$addr/2] = 0x$value, // $_";
	}
	else {
	    my $lsb = substr ($value, 4, 4);
	    my $msb = substr ($value, 0, 4);
	    print "  [$addr/2] = 0x$lsb, // $_";
	    print "  [$addr/2 + 1] = 0x$msb,\n";
	}
    }
    else {
	print "// ", $_;
    }
}

