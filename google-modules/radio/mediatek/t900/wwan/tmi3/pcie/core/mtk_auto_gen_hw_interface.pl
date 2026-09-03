#!/usr/bin/perl -s
# SPDX-License-Identifier: BSD-3-Clause-Clear
# Copyright (c) 2022, MediaTek Inc.

use strict;
use warnings;

my $output = $ARGV[0]; #param1
my $labels = $ARGV[1]; #param2

my @product = split(' ', $labels);#Array of labels
for(my $i = 0; $i <= $#product; $i++){
	print $product[$i]."\t";
}
print "\n";

open(my $output_fh, ">", "$output") || die "$0 : can't open $output for writing\n";
($_ = $0) =~ s%.*/%%; #% is separator instead of "/", because "/" show in matched string.
print $output_fh "/* SPDX-License-Identifier: BSD-3-Clause-Clear\n *\n * Copyright (c) 2022, MediaTek Inc.*/\n";
print $output_fh "/* DO NOT EDIT - the file is created automatically by ".$_."*/\n";
if($output =~ m%cldma%){
	for(my $i = 0; $i <= $#product; $i++){
		my @suffix = split('_', $product[$i]);
		if($#suffix == 1){
			print $output_fh "extern struct cldma_drv_ops drv_ops_name(".$suffix[0].");\n";
			print $output_fh "extern struct cldma_hw_regs cldma_regs_name(".$suffix[0].");\n";
		} else {
			last;
		}
	}
	print $output_fh "\n";
	print $output_fh "static struct cldma_drv_info_desc cldma_drv_info_tbl[] = {\n";
	for(my $i = 0; $i <= $#product; $i++){
		my @suffix = split('_', $product[$i]);
		if($#suffix == 1){
			print $output_fh "\t{".hex($suffix[1]).", &drv_ops_name(".$suffix[0]."), &cldma_regs_name(".$suffix[0].")},\n" ;
		} else {
			last;
		}
	}
	print $output_fh "\t{0, NULL},\n";
	print $output_fh "};\n";
}elsif($output =~ m%dpmaif%){
	for(my $i = 0; $i <= $#product; $i++){
		my @suffix = split('_', $product[$i]);
		if($#suffix == 1){
			print $output_fh "extern struct dpmaif_drv_ops drv_ops_name(".$suffix[0].");\n";
		} else {
			last;
		}
	}
	print $output_fh "\n";
	print $output_fh "static struct dpmaif_drv_ops_desc dpmaif_drv_ops_tbl[] = {\n";
	for(my $i = 0; $i <= $#product; $i++){
		my @suffix = split('_', $product[$i]);
		if($#suffix == 1){
			print $output_fh "\t{".hex($suffix[1]).", &drv_ops_name(".$suffix[0].")},\n" ;
		} else {
			last;
		}
	}
	print $output_fh "\t{0, NULL},\n";
	print $output_fh "};\n";
}elsif($output =~ m%ctrl%){
	for(my $i = 0; $i <= $#product; $i++){
		my @suffix = split('_', $product[$i]);
		if($#suffix == 1){
			print $output_fh "extern struct mtk_ctrl_info ctrl_info_name(".$suffix[0].");\n";
		} else {
			last;
		}
	}
	print $output_fh "\n";
	print $output_fh "static struct mtk_ctrl_info_desc mtk_ctrl_info_tbl[] = {\n";
	for(my $i = 0; $i <= $#product; $i++){
		my @suffix = split('_', $product[$i]);
		if($#suffix == 1){
			print $output_fh "\t{".hex($suffix[1]).", &ctrl_info_name(".$suffix[0].")},\n" ;
		} else {
			last;
		}
	}
	print $output_fh "\t{0, NULL},\n";
	print $output_fh "};\n";
}elsif($output =~ m%utility%){
	for(my $i = 0; $i <= $#product; $i++){
		my @suffix = split('_', $product[$i]);
		if($#suffix == 1){
			print $output_fh "extern struct mtk_utility_cfg utility_cfg_name(".$suffix[0].");\n";
		} else {
			last;
		}
	}
	print $output_fh "\n";
	print $output_fh "static struct mtk_utility_cfg_desc mtk_utility_cfg_tbl[] = {\n";
	for(my $i = 0; $i <= $#product; $i++){
		my @suffix = split('_', $product[$i]);
		if($#suffix == 1){
			print $output_fh "\t{".hex($suffix[1]).", &utility_cfg_name(".$suffix[0].")},\n" ;
		} else {
			last;
		}
	}
	print $output_fh "\t{0, NULL},\n";
	print $output_fh "};\n";
}else{
	for(my $i = 0; $i <= $#product; $i++){
		my @suffix = split('_', $product[$i]);
		my @sub_suffix = split('x', $suffix[1]);
		if($#suffix == 1){
			print $output_fh "extern const struct mtk_pci_dev_cfg mtk_dev_cfg_".$sub_suffix[1].";\n";
			if(hex($suffix[1]) == 0x4d75){
				print $output_fh "extern const struct mtk_pci_dev_cfg mtk_dev_cfg_4d80;\n" ;
			}
		} else {
			last;
		}
	}
	print $output_fh "\n";
	print $output_fh "static const struct  pci_device_id mtk_pci_ids[] = {\n";
	for(my $i = 0; $i <= $#product; $i++){
		my @suffix = split('_', $product[$i]);
		my @sub_suffix = split('x', $suffix[1]);
		if($#suffix == 1){
			print $output_fh "\tMTK_PCI_DEV_CFG(".$suffix[1].", mtk_dev_cfg_".$sub_suffix[1]."),\n" ;
			if(hex($suffix[1]) == 0x4d75){
				print $output_fh "\tMTK_PCI_DEV_CFG(0x4d80, mtk_dev_cfg_4d80),\n" ;
			}
		} else {
			last;
		}
	}
	print $output_fh "\t{/* end: all zeroes */}\n";
	print $output_fh "};\n";
}
close($output_fh);
