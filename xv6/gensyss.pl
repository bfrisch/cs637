#----------------------------------------------#
#  Ben's Awesome System Call Generator for xv6 #
#                  (BASC-GX)                   #
#     Questions/Comments: bfrisch@wisc.edu     #
#----------------------------------------------#
# Licensed under the MIT license:
# The MIT License
#
# Copyright (c) 2010 Benjamin Frisch
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
# THE SOFTWARE.
#----------------------------------------------#

use strict;

if (!(defined $ARGV[0])) {
    $ARGV[0] = 'user.h.in';
}
if (!open(IF, $ARGV[0])) {
    print stderr "Invalid parameter value.$/";
    usage();
}

if (!(defined $ARGV[1])) {
    $ARGV[1] = 'user.h';
}
if (!open(UH, '>'.$ARGV[1])) {
    print stderr "Invalid parameter value.$/";
    usage();
}
open(SJH, '>sysjump.h');
open(SH, '>syscall.h');
open(US, '>usys.S');

my $syscall_num = 0;
my $line_num = 0;
my $externs = "";
my $syscallArray = "";
my $headerMacro = fileNameToMacro($ARGV[1]);

# Write the top of all of the output files.
print(UH "#ifndef $headerMacro$/#define $headerMacro$/$/");
print(SH "#ifndef __SYSCALL_H_$/#define __SYSCALL_H_$/$/");
print(US "#include \"syscall.h\"$/#include \"traps.h\"$/$/#define STUB(name) \\$/  .globl name; \\$/  name: \\$/    movl \$SYS_ ## name, %eax; \\$/    int \$T_SYSCALL; \\$/    ret$/$/");
print(SJH "#ifndef __SYSJUMP_H_$/#define __SYSJUMP_H_$/$/");

while (<IF>) {
    chomp;
    $line_num++;
    s/(#|\/\/).*//; # Remove comments that start with # or //
    s/^\s*|\s*$//; # Trim what's left.
    if (length $_ > 0) {
	if ($_ =~ /\s*((\**\w+\**\s+)+\**)(\w+)\s*(\(.*\))\s*\;*\s*(:?)\s*(\w*)(.*)/) {
	    if (length $3 eq 0 or length $7 > 0) {
		print(stderr "Line $line_num seems like a function definition, but is actually formatted incorrectly.$/\tSkipping.$/");
		next;
	    }
	    # Put the function name as specified in user.h
	    print(UH "$1$3$4;$/");

	    # Add the syscall to variable that contains the definition for the jump array and generate the extnernal function for the sycall implementation.    
	    if (length $5) {
		if (length $6) {
		    $externs.="extern $1$6(void);$/";
		    $syscallArray.="[SYS_$3] $6,$/";
		} else {
		    next;
		}
	    } else {
		$externs.="extern $1sys_$3(void);$/";
		$syscallArray.="[SYS_$3] sys_$3,$/"
	    }

	    # Add the syscall to syscall.h
	    $syscall_num++;
	    print SH "#define SYS_$3\t$syscall_num$/";
	    
	    # Add the stub for the system call.
	    print US "STUB($3)$/";
	} elsif ($_ =~ /\s*\?\s*(.+)\s*/) {
	    # Close the ifdef in the previous header file and switch 
	    # to the new header file.
	    print(UH "$/#endif /* $headerMacro */$/");
	    close(UH);
            open (UH, ">$1");
	    $headerMacro = fileNameToMacro($1);
	    print(UH "#ifndef $headerMacro$/#define $headerMacro$/$/");
	} else {
	    print(stderr "Line $line_num does not appear to be blank, a full line comment, or a function prototype.$/\tAdding the trimmed line without comments to $ARGV[1] anyway.$/");
	    print(UH $_.$/);
	}
    }
}

# Actually generate the core of sysjump.h from the variables.
print(SJH "$externs$/int (*syscalls[])(void) = {$/$syscallArray};$/$/#endif /* __SYSJUMP_H_ */$/");

# Close the ifndef.
print(SH "#endif /* __SYSCALL_H_ */$/");
print(UH "$/#endif /* $headerMacro */$/");

close(SJH);
close(SH);
close(UH);
close(IF);
close(US);

sub usage {
    printf stderr "perl gensyss.pl <input_file> <output_file>$/";
    exit();
}

sub fileNameToMacro {
    my $fileName = shift;
    if ($fileName =~ /(.+)\.h/) {
	return("\U__$1_H_");
    } else {
	return("\U__$fileName".'_');
    }
}
