#!/usr/bin/perl -s
# SPDX-License-Identifier: BSD-3-Clause-Clear
# Copyright (c) 2023, MediaTek Inc.

use strict;
use warnings;
use FindBin '$Bin';

my $output = $ARGV[0]; #param1
open(my $output_fh, ">", "$output") || die "$0 : can't open $output for writing\n";

my $build_label_file = "$Bin/label.ini";
open(my $label_fd, '<', $build_label_file) or die "can't open $build_label_file for reading";
my $build_label = <$label_fd>;
chomp($build_label);

($_ = $0) =~ s%.*/%%; #% is separator instead of "/", because "/" show in matched string.
print $output_fh "/* SPDX-License-Identifier: BSD-3-Clause-Clear\n *\n * Copyright (c) 2023, MediaTek Inc.\n */\n";
print $output_fh "/* DO NOT EDIT - the file is created automatically by ".$_." */\n";
print $output_fh "#define BUILD_INFO_STR \"build label:$build_label\"";
print $output_fh "\n";

close($label_fd);
close($output_fh);

