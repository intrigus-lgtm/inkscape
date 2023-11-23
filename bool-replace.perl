# SPDX-License-Identifier: GPL-2.0-or-later
#!/usr/bin/perl

# Restore use of True (instead of '1') and False (instead of '0') after
# using gtk-builder-tool or gtk4-builder-tool.

use strict;
use warnings;
use File::Copy;

open my $true_false, '<', "true_false.txt" or die "Cannot read true_false.txt";
chomp (my @properties = <$true_false>);

my $pattern = "";
foreach (@properties) {
    $pattern = $pattern . "\"";
    $pattern = $pattern . $_;
    $pattern = $pattern . "\"";
    $pattern = $pattern. "|";
}
chop $pattern;
#print $pattern . "\n";

my $file = $ARGV[0];
#print $file . "\n";

my $file_out = $file . "_out";
open my $input, '<', $file or die "Cannot open $file";
open my $output, '>', $file_out or die "Cannot open $file_out";

my $class = "";
while (my $line = <$input>) {

    # Track current class
    if ($line =~ m/class=\"(.*?)\"/) {
        $class = $1;
        # print "Class: " . $class . "\n";
    }

    # For gtk4-builder-tool
    $line =~ s/translatable=\"1\"/translatable=\"yes\"/;
    $line =~ s/translatable=\"0\"/translatable=\"no\"/;

    if ($line =~ m/($pattern)/) {
        #  print $line;
        if ($line =~ m/active/ and $class =~ m/ComboBox/) {
            # Don't change 'active' property to True/False for ComboBox or ComboBoxText
            # print "FOUND COMBOBOX WITH ACTIVE\n";
        } else {
            $line =~ s/>0</>False</;
            $line =~ s/>1</>True</;
        }
        # print $line;
    }
    $line =~ s/&quot;/\"/g;  # This is improper... but it would affect translations.
    $line =~ s/&apos;/\'/g;
    print $output $line;
}

close $input;
close $output;
move ($file_out, $file) or die "Couldn't move $file_out to $file!";
