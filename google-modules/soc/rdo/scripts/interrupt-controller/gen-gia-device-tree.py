#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
#
# Copyright 2024 Google LLC

import argparse
import csv
import logging
import os
import re

compatible_strings = {
	"level" : 	"google,level-sgia",
	"multicast" : 	"google,level-sgia",
	"pulse" : 	"google,pulse-sgia",
}

sswrp_base_addr_map = {}

# Not all SSWRPs give addr data in intr tracker.
# I manually compiled address data for the nodes going to AP_NS which don't
# BUG: b/364987208
manual_addr_map = {}

gia_dict = {}

# This is an invalid GIA addr - GIAs must be aligned to 0x20
# This is used to insert a GIA which is missing address data
# Nodes with addresses ending with this must be disabled in output
# BUG: b/364987208
ERR_ADDR = "ffff"

# Used for disabling nodes where we did not find a macro string
ERR_MACRO_STR = "0>; // ERROR"

ERR_FINAL_DST = '_'

class gia:
	name = ""
	gia_type = ""
	sswrp = ""
	# sometimes (mostly in AOSS) the GIA a node outputs to is in a different SSWRP
	output_sswrp = ""
	addr = ""
	power_domain = ""
	final_dst = [] # list in case of multicast
	output = [] # list in case of multicast
	# To tell whether a GIA feeds into another GIA or an interrupt controller
	# if it's true, we don't need an interrupt-parent property in the DT
	output_is_GIC_NVIC = False
	input_gias = []
	# macro_str is the output interrupt connection
	macro_str = ""

	# This is the constructor to build a GIA node from the address map file
	def __init__(self, sswrp, name, addr, type):
		self.sswrp = sswrp
		self.name = standardize_gia_name(sswrp, name)
		self.gia_type = type.lower().strip()

		try:
			int(sswrp_base_addr_map[sswrp], 16)
		except:
			logging.debug('ERROR: unable to convert addr for sswrp {}, name {}, addr {}, type {}'.format(sswrp, name, addr, type))
			return

		self.addr = hex(int(sswrp_base_addr_map[sswrp], 16) + int(addr, 16))
		self.output = []
		self.output_is_GIC_NVIC = False
		self.power_domain = ""
		self.final_dst = []

	def __str__(self):
		return f"name: {self.name}, type: {self.gia_type}, addr: {self.addr}, final_dst: {self.final_dst}, output: {self.output}, output_sswrp: {self.output_sswrp}, power: {self.power_domain}"

	# returns a string in device tree format, or blank if the GIA does not output to dest
	# We need dest because we only want to make a device tree for one domain at a time (AP_NS)
	def dt_node(self, dest):
		if not dest in self.final_dst:
			return ""

		node_str = f"\t{self.name}: {self.name}@{self.addr.removeprefix('0x')} {{\n"
		node_str += f"\t\tcompatible = \"{compatible_strings[self.gia_type]}\";\n"
		node_str += "\t\tinterrupt-controller;\n"
		if not self.output_is_GIC_NVIC:
			parent = self.output[0]
			if len(self.output) > 1:
				parent = find_dest_output(self, dest)
			# If we may already have standardized the name - don't double prefix
			if self.output_sswrp == '' and parent.startswith('raw'):
				parent = standardize_gia_name(self.sswrp, parent)
			elif parent.startswith('raw'):
				parent = standardize_gia_name(self.output_sswrp, parent)
			node_str += f"\t\tinterrupt-parent = <&{parent}>;\n"
		node_str += "\t\t#interrupt-cells = <1>;\n"
		node_str += f"\t\treg = <0x0 {self.addr} 0x0 0x20>;\n"
		node_str += f"\t\tinterrupts = <{self.macro_str}>;\n"
		if self.power_domain:
			node_str += f"\t\tpower-domains = <&{self.power_domain}>;\n"

		# b/364987208
		# For GIAs we know have wrong info, print them disabled.
		if self.addr.endswith(ERR_ADDR) or self.macro_str == ERR_MACRO_STR:
			node_str += "\t\tstatus = \"disabled\";\n"

		node_str += "\t};\n"

		return node_str

def standardize_gia_name(sswrp, raw_name):
	# this is taking in the value in map, output - may need more flexibility for input file
	# gia_map format is {DOMAIN}.{type}_aggr_{#}_intr
	# launch_o src format is {type}_aggr_intr_{#}
	# launch_o dst format is raw_{type}_{#}[{port}]

	if "raw" in raw_name:
		name = re.sub("raw", sswrp, raw_name)
		name = re.sub("_intr", "_aggr", name)
		match = re.search(r'(.*)\[\d+\]', name)
		if match:
			name = match.group(1)
	else:
		name = re.sub("_intr", "", raw_name.split('.')[-1])
		name = sswrp + '_' + name

	return name

def find_gia_by_name(standardized_name):
	try:
		return gia_dict[standardized_name]
	except:
		logging.debug("ERROR: No GIA for name {} found".format(standardized_name))
		return None

def extract_sswrp(fname):
	#in, out csvs match with launch, the addr csv matches gia_map
	match = re.search(r'sswrp_(.*?)_(?:launch|gia)_', fname)
	if match:
		sswrp = match.group(1)

		# manual corrections for these two
		if sswrp == 'memss_top':
			sswrp = 'memss'
		if sswrp == 'aurdsp':
			sswrp = 'aurora'
		# defense against HSIO_S vs HSIOS
		if 'sio_' in sswrp:
			sswrp = sswrp.replace('_','')

		return sswrp

	if fname.lower().startswith('cpm_m55'):
		return 'cpm_m55'

	logging.debug("ERROR: Unable to extract sswrp from file name " + fname)
	return None

def find_dest_output(gia_node, dest):
	if len(gia_node.final_dst) == 1:
		if dest != gia_node.final_dst[0]:
			logging.debug("error: {} not in destinations for {}".format(dest, gia_node.name))
			return ''
		else:
			return standardize_gia_name(gia_node.output_sswrp, gia_node.output[0])

	for o in gia_node.output:
		output = find_gia_by_name(standardize_gia_name(gia_node.output_sswrp, o))
		if not output:
			logging.debug("Error: could not find node for {} + {}".format(gia_node.output_sswrp, o))
		elif dest in output.final_dst:
			return output.name

	logging.debug("error: did not find node leading to {} from {}".format(dest, gia_node.name))
	return ''

# filter out irrelevant lines
def is_gia_line(line):
	if not line:
		return False

	# ignore first line
	if "SRC Port" in line:
		return False

	if "Offset" in line:
		return False

	if "feedthrough" in line[1] or "feedthrough" in line[3]:
		return False

	return True

def populate_sswrp_base_addr_map(csvfile):
	# CSV order is this:
	# fabric, sswrp, range start , range end, size, xmb chunks
	csv_reader = csv.reader(csvfile)
	for line in csv_reader:
		match = re.search(r'SSWRP_(.*)', line[1])
		if not match:
			continue

		sswrp = match.group(1).lower()

		# there are three memss entries - this is the one with GIAs.
		if sswrp == "memss(other)":
			sswrp = "memss"
		#two CPU entries - this is the one with GIAs
		if sswrp == "cpu(tower)":
			sswrp = "cpu"
		elif sswrp == "gcve":
			sswrp = "gcv"
		elif sswrp == "aurdsp":
			sswrp = "aurora"
		elif sswrp == "memss(gmc_lanes_com)":
			sswrp = "gmc"
		# Defend against HSIO_S and HSIOS both being used
		elif 'sio_' in sswrp:
			sswrp = sswrp.replace('_','')

		base_addr = line[2]

		sswrp_base_addr_map[sswrp] = base_addr

	# annoying special cases. Hardcoded.
	sswrp_base_addr_map['aoss_pg'] = sswrp_base_addr_map['aoss']
	sswrp_base_addr_map['aoss_aon'] = sswrp_base_addr_map['aoss']
	sswrp_base_addr_map['aoss_aonss'] = sswrp_base_addr_map['aoss']
	sswrp_base_addr_map['aoss_ambss'] = sswrp_base_addr_map['aoss']

def build_gias_from_gia_map_csv(csv_path):
	base_sswrp = extract_sswrp(os.path.basename(csv_path))
	if not base_sswrp:
		logging.debug("Error: could not extract sswrp for " + str(csv_path))
		return

	with open(csv_path, 'r') as csvfile:
		csv_reader = csv.reader(csvfile)
		for line in csv_reader:
			if not is_gia_line(line):
				continue

			name, addr, type = line[0], line[1], line[2]

			domain = '.'.join(name.split('.')[:-1])
			sswrp = get_domain_sswrp(domain, base_sswrp)

			gia_node = gia(sswrp, name, addr, type)
			gia_dict[gia_node.name] = gia_node

def get_domain_sswrp(domain, backup_sswrp):
	# There is no consistent way to do this. This looks hacky because it is hacky
	# see discussion at b/366541953

	# special cases everywhere in AOSS
	if domain.endswith('u_sswrp_aoss_pg_intr'):
		return 'aoss_pg'
	elif domain.endswith('u_sswrp_aon_gia.u_gia'):
		return 'aoss_aon'
	elif domain.endswith('u_aonss_gia_wrap.u_gia'):
		return 'aoss_aonss'
	elif domain.endswith('u_ambss_gia_wrap.u_gia'):
		return 'aoss_ambss'

	# This is a best-effort attempt to be data-driven and find the sswrp from the data
	domain = domain.removesuffix('.u_gia')
	domain = domain.split('.')[-1]

	match = re.search(r'u_(sswrp_)?(.*)(_aux|_top)?(_aon|_pg)?_gia', domain)
	if match:
		domain = match.group(2)
	else:
		logging.debug("WARNING: could not extract sswrp from " + domain)
		domain = ''

	# always check against base addr keys for valid sswrp - if it's not in there it's not right
	if domain not in sswrp_base_addr_map.keys():
		logging.debug("INFO: falling back to file name " + backup_sswrp)
		domain = backup_sswrp

	return domain

def populate_gia_outputs_from_csv(csv_path):
	base_sswrp = extract_sswrp(os.path.basename(csv_path))
	if not base_sswrp:
		logging.debug("Error: could not extract sswrp for " + str(csv_path))
		return

	with open(csv_path, 'r') as csvfile:
		csv_reader = csv.reader(csvfile)
		for line in csv_reader:
			if not is_gia_line(line):
				continue

			# some old labels have more than 4 columns, so don't collapse these assignments into one line
			src_domain = line[0]
			name = line [1]
			dst_domain = line[2]
			output = line[3]

			# src and dst sswrp handling is here for a few reasons
			# 1. Some GIAs route into other SSWRPs. Mostly AOSS, with its several different launch_*_o files.
			# 	In these cases we need to avoid ambiguity and name collisions.
			#	There could be an aoss_pg_level_aggr_1 and aoss_aon_level_aggr_1.
			# 2. Then we back out this for sswrps which buck convention in their launch_o files
			# 	Investigate DPU, codec_3p, aurora, gcv files to see that "just" extracting sswrp from a string
			# 	can get very annoying. Fortunately these ones do not route into other SSWRPs,
			#	So we can use the sswrp from the file name.
			src_sswrp = get_domain_sswrp(src_domain, base_sswrp)

			if dst_domain == '':
				dst_sswrp = ''
			else:
				dst_sswrp = get_domain_sswrp(dst_domain, base_sswrp)

			standard_name = standardize_gia_name(src_sswrp, name)

			gia_node = find_gia_by_name(standard_name)

			# Some SSWRPs do not yet have addr data. We will mock it here.
			# The error addresses can be fixed by passing in a manually-generated map
			# b/364987208
			if not gia_node:
				try:
					addr = manual_addr_map[standard_name]
				except:
					logging.debug(standard_name + " not in manual_addr_map")
					addr = ERR_ADDR

				# some sswrps will have underscores, so we can't .split('_')[1]
				# removeprefix will leave the first '_', so still take index [1]
				type = standard_name.removeprefix(src_sswrp).split('_')[1]
				gia_node = gia(src_sswrp, name, addr, type)
				gia_dict[gia_node.name] = gia_node

			gia_node.output.append(output)
			gia_node.output_sswrp = dst_sswrp

			# This means that the GIA outputs to GIC or NVIC. Set the final dst appropriately
			if not dst_domain:
				gia_node.output_is_GIC_NVIC = True

				# remove optional port number
				output_stripped = output.split('[')[0]

				#Hack for TPU. TODO Bug #
				output_stripped = output_stripped.removesuffix('_grp0')

				match = re.search(r'gia_out_(.+)', output_stripped)

				if match:
					if match.group(1).endswith('ap_0'):
						logging.debug("replacing ap_0 dst with ap_ns")
						gia_node.final_dst.append('ap_ns')
					gia_node.final_dst.append(match.group(1))
				# handle known special cases in the tracker data
				elif output.endswith('fab_cpm_swpg_ack'):
					gia_node.final_dst.append('cpm')
				elif output.startswith('cpu') and output.endswith('intr'):
					gia_node.final_dst.append(output.removeprefix('cpu_').removesuffix('_intr'))
				elif output.startswith('gdmc') and output.endswith('aggr_irq'):
					gia_node.final_dst.append(output.removeprefix('gdmc_').removesuffix('_aggr_irq'))
				else:
					logging.debug("error: could not extract final dst from " + output)
					gia_node.final_dst.append(output)

			# this case is outputting to an internal NVIC. We should ignore it
			# setting err_final_dst so we can look at them and all GIAs rolling up to them
			# this is lines with output like "BInterrupt[port]" or 'sys_irq[port]"
			if dst_domain != '' and '_intr' not in output:
				logging.debug("Setting error final dst on " + name)
				logging.debug(line)
				gia_node.output_is_GIC_NVIC = True
				gia_node.final_dst += ERR_FINAL_DST

def hack_power_domains():
	# for use when hacking power domains. These are manually pulled from malibu-base.dtsi power_controller
	# This is a hack. Correct solution is for us to get a list of PDs per GIA node
	# b/362349471
	power_domain_hacks = {
		"aurora":	"sswrp_aurdsp_pd",
		"codec_3p":	"sswrp_codec_3p_pd",
		"dpu":		"sswrp_dpu_pd",
		"g2d":		"sswrp_g2d_pd",
		"gcv":		"sswrp_gcv_pd",
		"gpu":		"sswrp_gpu_pd",
		"hsion":	"sswrp_hsio_n_pd",
		"hsios":	"sswrp_hsio_s_pd",
		"ispbe":	"sswrp_ispbe_pd",
		"ispfe":	"sswrp_ispfe_pd",
		"lsioe":	"sswrp_lsio_e_pd",
		"lsios":	"sswrp_lsio_s_pd",
		"pcie":		"sswrp_pcie_pd",
		"tpu":		"sswrp_tpu_pd",
	}

	for name, g in gia_dict.items():
		if (g.sswrp in power_domain_hacks.keys()):
			g.power_domain = power_domain_hacks[g.sswrp]

def add_power_domains_from_csv(csvfile):
	csv_reader = csv.reader(csvfile)
	for line in csv_reader:
		g = find_gia_by_name(line[0])
		g.power_domain = line[1]

def add_gic_vectors(dest_domain, soc):
	# TODO needs chipset arg. Currently only for MBU so hardcoded
	# Location of header files is hardcoded
	include_path = os.path.join(os.path.dirname(__file__), '../../include/dt-bindings/interrupt-controller')
	gic_path = os.path.join(include_path, soc.lower() + '-gic.h')

	with open(gic_path, 'r') as gic_file:
		#load entire file into memory. This is ~40 kB
		contents = gic_file.read()
		for name, gia_node in gia_dict.items():
			# GIC macros only go on GIAs going directly to GIC
			if not gia_node.output_is_GIC_NVIC:
				continue
			if dest_domain not in gia_node.final_dst:
				continue

			output = gia_node.output[0]
			if len(gia_node.output) > 1:
				logging.debug("Error: expected final GIAs to have 1 output only" + str(gia_node))

			output = output.replace('[','_').replace(']','')

			# THere are multiple GMC macros. We want GMC0
			if output.startswith('gmc'):
				output = output.replace('gmc','gmc0')

			gic_regex = re.compile("SSWRP_.*" + output.upper())

			match = re.search(gic_regex, contents)
			if not match:
				logging.debug("ERROR: No match for " + str(gia_node) + " in GIC header. Update the header?")
				logging.debug("Regex: " + str(gic_regex))
				gia_node.macro_str = ERR_MACRO_STR
				continue

			gia_node.macro_str = "GIC_SPI " + match.group(0) + " IRQ_TYPE_LEVEL_HIGH 0"


def add_gia_vectors(dest_domain, soc):
	# TODO needs chipset arg. Currently only for MBU so hardcoded
	# Location of header files is hardcoded
	include_path = os.path.join(os.path.dirname(__file__), '../../include/dt-bindings/interrupt-controller')
	gia_path = os.path.join(include_path, 'irq-gia-google-' + soc.lower() + '.h')

	with open(gia_path, 'r') as gia_file:
		#load entire file into memory. This is ~500 kB
		contents = gia_file.read()
		for name, gia_node in gia_dict.items():
			# GIA macros only go on GIAs outputting into another GIA
			if gia_node.output_is_GIC_NVIC:
				continue
			if dest_domain not in gia_node.final_dst:
				continue

			# target GIA macro looks like DPU_GIA_LEVEL_AGGR_0_AUX_AON_GIA_GIA_GIA_MULTICAST_AGGR_INTR_0
			# for input GIA dpu_multicast_aggr_0 to output GIA dpu_level_aggr_0
			# we're going to use the gia names to build a regex and find that macro string

			output = find_dest_output(gia_node, dest_domain).upper()
			output = output.split('_')

			insert_index = 1

			if output[0] == "AOSS":
				#AOSS always has a suffix, remove it
				output.pop(1)
			elif output[0] == "CODEC":
				# insert after _3P_ to properly find macros
				insert_index = 2

			output.insert(insert_index,"GIA")
			output = '_'.join(output)

			input = name.upper()
			input = input.split('_')
			input.insert(-1, "INTR")

			# double pop because of suffix (AOSS_AMBSS_, CODEC_3P...)
			if(input.pop(0) in ["AOSS", "CODEC"]):
				input.pop(0)

			input = '_'.join(input)

			gia_regex = re.compile(output + ".*" + input)

			match = re.search(gia_regex, contents)
			if not match:
				logging.debug("ERROR: No match for " + str(gia_node))
				logging.debug("Regex: " + str(gia_regex))
				gia_node.macro_str = ERR_MACRO_STR
				continue

			gia_node.macro_str = match.group(0)

def find_final_dst(gia_node):
	if not gia_node:
		logging.debug("ERROR: need a node to find final dst")
		return []

	if gia_node.final_dst:
		return gia_node.final_dst

	for o in gia_node.output:
		dst = find_final_dst(find_gia_by_name(standardize_gia_name(gia_node.output_sswrp, o)))
		if dst:
			gia_node.final_dst += dst

	if len(gia_node.final_dst) == 0:
		gia_node.final_dst += ERR_FINAL_DST

	return gia_node.final_dst

# this function tags each GIA node with the GIC/NVIC destinations its output traces to
def propagate_final_dst():
	# this does something like a depth first coloring
	for name, gia_node in gia_dict.items():
		if find_final_dst(gia_node) == []:
			logging.debug("error: could not trace final dst for " + name)
			for o in gia_node.output:
				logging.debug(standardize_gia_name(gia_node.sswrp, o))
			logging.debug(gia_node)

def generate_device_tree(dest_domain, soc):
	print('// SPDX-License-Identifier: GPL-2.0-only OR BSD-3-Clause')
	print('#include <dt-bindings/interrupt-controller/' + soc.lower() + '-gic.h>')
	print('#include <dt-bindings/interrupt-controller/irq-gia-google-' + soc.lower() + '.h>')
	print('\n/***** THIS FILE GENERATED USING {} *****/\n'.format(os.path.basename(__file__)))
	print('/ {')
	for name, gia_node in gia_dict.items():
		str = gia_node.dt_node(dest_domain)

		# GPCA is specifically excluded from MBU: b/428046594
		if soc.lower() == 'mbu' and gia_node.sswrp.startswith('gpca'):
			continue

		# Printing when a GIA node doesn't go to the domain (returns "") would give a blank line Skip these.
		if str:
			print(str)

	print('};')

def generate_disable_nodes(dest_domain, enable_list):
	for name, gia_node in gia_dict.items():
		if gia_node.sswrp in enable_list:
			continue

		# this is not great - should extract the functionality to see if it's routing to the domain
		if gia_node.dt_node(dest_domain):
			print('&' + name + ' {')
			print('\tstatus = "disabled";')
			print('};\n')

			# also disable the test node - test tree gen is only off the .dtsi
			print('&' + name + '_test {')
			print('\tstatus = "disabled";')
			print('};\n')

def build_manual_addr_map(addr_csv):
	# expected format: name, addr
	csv_reader = csv.reader(addr_csv)
	for line in csv_reader:
		name, addr = line
		manual_addr_map[name] = addr

def add_manual_gias():
	# Hack to manually add GIAs which the script is not processing for whatever reason.
	# bug: b/370766015
	aoss_multi_11 = gia('aoss_ambss', 'multicast_aggr_11', '0x2905000', 'level')
	aoss_multi_11.final_dst = 'ap_ns'
	aoss_multi_11.output_is_GIC_NVIC = True
	aoss_multi_11.output = ['aoss_ap_intr_ns_aossambss[0]']
	aoss_multi_11.output_sswrp = 'aoss_aon'
	gia_dict[aoss_multi_11.name] = aoss_multi_11

def get_fnames_by_pattern(root_dir, pattern):
	#dict to defend against duplicate files
	file_dict = {}

	for dirpath, _, filenames in os.walk(root_dir):
		for filename in filenames:
			if filename.endswith(pattern):
				file_dict[filename] = os.path.join(dirpath, filename)

	sorted_list = [file_dict[i] for i in sorted(file_dict)]
	return sorted_list

def find_output_csvs(root_dir):
	# not launch_o.csv because of sswrp_aoss_launch_ambss_o.csv, etc (AOSS does this and TPU does this)
	return get_fnames_by_pattern(root_dir, '_o.csv')

def find_gia_map_csvs(root_dir):
	return get_fnames_by_pattern(root_dir, '_gia_map.csv')

def main():
	parser = argparse.ArgumentParser()
	parser.add_argument('-i', '--intr_tracker', dest='intr_tracker_root',
				required=True, help='Path to the base interrupt tracker dir')
	parser.add_argument('-b', '--base_addr_map', type=argparse.FileType('r'), dest='sswrp_addrs_csv',
				required=True, help='Path to the base addrs per sswrp csv')
	parser.add_argument('-s', '--soc', dest='soc', default='mbu',
				help='short SOC name to generate header for')
	parser.add_argument('-p', '--power_domains', type=argparse.FileType('r'),
				dest='power_domains', help='Path to the power domains per GIA csv')
	parser.add_argument('-a', '--addresses', type=argparse.FileType('r'),
				dest='manual_addr_csv', help='Path to the manual address per GIA csv')
	parser.add_argument('-d', dest='debug', action='store_true',
				help='Enable debugging log (default: off)')
	parser.add_argument('-H','--hack-pds', dest='hack_power_domains',
				action='store_true', help='Set power domains to \'sswrp_<sswrp>_pd\'')
	parser.add_argument('--disable_sswrps_except', dest = 'enable_list',
				help='Instead of device tree, generate a disable file for all except these SSWRPS. Includes test node disables')
	args = parser.parse_args()

	if args.debug:
		logging.getLogger().setLevel(logging.DEBUG)

	output_csvs = find_output_csvs(args.intr_tracker_root)
	gia_map_csvs = find_gia_map_csvs(args.intr_tracker_root)

	# not all GIAs give addr data in intr tracker
	# b/364987208
	if args.manual_addr_csv:
		build_manual_addr_map(args.manual_addr_csv)

	populate_sswrp_base_addr_map(args.sswrp_addrs_csv)

	for csv in gia_map_csvs:
		build_gias_from_gia_map_csv(csv)

	for csv in output_csvs:
		populate_gia_outputs_from_csv(csv)

	add_manual_gias()

	if args.hack_power_domains:
		hack_power_domains()

	# This will overwrite any hacked power domains. They can safely be used at the same time
	if args.power_domains:
		add_power_domains_from_csv(args.power_domains)

	propagate_final_dst()

	add_gic_vectors('ap_ns', args.soc)
	add_gia_vectors('ap_ns', args.soc)

	if args.enable_list:
		generate_disable_nodes('ap_ns', args.enable_list)
	else:
		generate_device_tree('ap_ns', args.soc)


	return 0

if __name__ == "__main__":
	main()
