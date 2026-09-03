#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
#
# Copyright 2024-2025 Google LLC

import os
import datetime
import csv
import sys
import re

sswrp_str = ""
column_width = 97

# Maintain dictionary of words that needs to be filtred out.
# These words not adding a lot of value but string length
# increases if we include them.
dictionary = {
	"sswrp",
	"wrap",
	"pg",
	"u",
	"apbscfgxportauxaxistrm",
	"customblock",
	"customblockfunctionaltop",
	"rqstr",
	"subsys",
	"ret",
	"ace5lite"
}

# There are some o/p line which has compilation issues. Those
# issues if cannot be fixed in the script, list here, so this
# script won't generate them
compilation_issues = {
	"GPCM_GIA_LEVEL_AGGR_0_U_GPCM_LPM_EXCP_IRQ", #duplicate lines in intr_tracker database
	"DPU_GIA_LEVEL_30_AGGR_DUMMY_GIA_PULSE_AGGR_8_INTR", #duplicate lines in intr_tracker database
}

# Do a final scan on o/p line before it gets printed
def check_final_audit(line):
	for bad_line in compilation_issues:
		if bad_line in line:
			return True

	return False

# Align numbers to fixed width
def align_line(x_string, yy_string):
	global column_width
	total_spaces = column_width - len(x_string)
	if total_spaces <= 0:
		total_spaces = 1

	output_line = f"{x_string}{' ' * (total_spaces)}{yy_string}"
	return output_line  # Join the elements into a single string

# Print o/p line with proper formatting
def print_nice_line(sswrp, gia, irq_name, hwirq):
	if "SSWRP" in sswrp:
		sswrp = sswrp.split('SSWRP_')[1]

	prefix = "#define " + sswrp + "_" +  gia + "_" + irq_name
	suffix=hwirq
	result = align_line(prefix, suffix)
	if check_final_audit(result):
		#print("//Invalid line: ", result)
		result = ""
	else:
		print(result)

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

	#print("ERROR: Unable to extract sswrp from file name " + fname)
	return None


# Extract SSWRP from a filename
def set_current_sswrp(file_path):
	global sswrp_str
	sswrp_str = extract_sswrp(os.path.basename(file_path)).strip().upper()

	tokens = sswrp_str.split('_')
	for token in tokens:
		dictionary.add(token.lower())

	print("")
	print("/* ", sswrp_str, " */", sep="")

def extract_trailing_number(text):
	match = re.search(r"(\d+)$", text)  # Search for one or more digits at the end
	if match:
		return int(match.group(1))  # Extract and convert to an integer
	else:
		return -1  # Indicate no number found

def extract_number_part_of_string(text):
	match = re.search(r'_(\d+)_', text)  # Search for a digit surrounded by underscores
	if match:
		return int(match.group(1))
	else:
		return None

# Some strings are too long with a lot of redundant words. It's
# good to filter them out and only keep minimal but important
# info
def extract_meaningful_info(text):
	delimiters = ['.', '_', '[', ']', ' ', '(', ')', ',']
	words = re.split(r'|'.join(map(re.escape, delimiters)), text)

	mismatched_words = []
	for word in words:
		if word.strip() != "" and word.lower() not in dictionary:
			mismatched_words.append(word)

	if mismatched_words:
		return '_'.join(mismatched_words).strip()
	else:
		return text  # All words found

def split_text_and_number(text):
	# If string doesn't have a square bracket, we ignore that line.
	patterns = [
		r"^(?P<prefix>.*)\[(?P<number>\d+)\]$",  # Bracket format
		r"^(?P<prefix>.*)\[(?P<number1>\d+):(?P<number2>\d+)\]$"  # Colon format
	]

	for pattern in patterns:
		match = re.match(pattern, text)
		if match:
			if "number1" in match.groupdict():
				return match.group("prefix").upper(), f"{match.group('number1')}:{match.group('number2')}"
			else:
				return match.group("prefix").upper(), match.group("number")

	return None, None

def generate_macros(tokens):
	badline = False
	length = len(tokens)
	i = 0
	gia = "something"
	gia_instance = -1
	hwirq = "-1"

	for token in tokens:
		if "raw" in token:
			gia_raw, hwirq = split_text_and_number(token)
			break;

		i = i + 1

	if gia == None or hwirq == "-1" or gia_raw == None:
		#print("// Ignore this line (No interrupt information found): ", tokens)
		return

	gia = gia_raw.replace("RAW", "GIA").replace("INTR", "AGGR")

	# !!Exception!! - DPU has different way of representing wide
	#				 GIA. So, handle that.
	global sswrp_str

	# Normal case
	if (gia_instance == -1):
		# Check for wide GIA case from destination
		gia_instance = extract_trailing_number(tokens[i - 1])

	irq_name = tokens[i - 2].strip().upper()
	#print("// irq_name: ", irq_name)

	if (length >= 5) and tokens[i - 4] != "":
		all_prefixes = tokens[i - 3] + "_" + tokens[i - 4]
	else:
		all_prefixes = tokens[i - 3]

	source_instance = extract_meaningful_info(all_prefixes).upper()

	if gia_instance == -1:
		gia_instance = 0

	if "[" in irq_name or "]" in irq_name or "[" in gia or "]" in gia:
		badline = True
		#print("//complicated input line : ", sswrp_str, "_",  gia, "_", irq_name, "\t\t", hwirq, sep="")

	# Use regex and determine square bracket
	pattern = r"^(.*)\[(\d+):?(\d*)\]"
	match = re.match(pattern, irq_name)
	if match: # when irq_name has square brackets []
		prefix = match.group(1)
		end = int(match.group(2))

		if match.group(3):  # Check if a range was specified
			start = int(match.group(3))
		else:
			start = end  # Single number case

		base_hwirq = int(hwirq.split(":")[0]) - end + start

		# Range expansion
		for i in range(start, end + 1):
			print_nice_line(sswrp_str, gia, f"{source_instance}_{prefix}_{i}", base_hwirq + i - start + (32 * gia_instance))

	else: # normal case
		irq_name = source_instance + "_" + extract_meaningful_info(irq_name) # Pick differentiator info from 1-st arg
		hwirq = extract_trailing_number(hwirq)
		print_nice_line(sswrp_str, gia, irq_name, int(hwirq) + (32 * gia_instance))

def validate_tokens(tokens):
	if not tokens:  # Handle empty list case
		#print("//Ignore this line (no tokens): ", tokens)
		return False

	i = 0
	length = len(tokens)

	for token in tokens:
		if token.strip():
			break;
		else:
			i = i + 1

	if i == length:
		#print("//Ignore this line (all empty tokens): ", tokens)
		return False

	i = 0
	for token in tokens:
		# Combine common invalidity checks
		# Remove empty entries
		# Remove entries with TZ
		if "'b0" in token or "tz" in token.lower() or "feedthrough" in token.lower():
			#print("//Ignore this line (b0 or tz or feedthrough): ", tokens)
			return False

		if ("DST" in token.upper() and "PORT" in token.upper()) or ("SRC" in token.upper() and "PORT" in token.upper()):
			#print("//Ignore this line (heading): ", tokens)
			return False;

		# Don't make macros for the aggregators which come *after* GIC
		if ("u_gsa_gia" in token.lower()):
			return False;

		i = i + 1

	return True  # All tokens passed validation

def get_fnames_by_pattern(root_dir, pattern):
	#dict to defend against duplicate files
	file_dict = {}

	for dirpath, _, filenames in os.walk(root_dir):
		for filename in filenames:
			if filename.endswith(pattern):
				file_dict[filename] = os.path.join(dirpath, filename)

	sorted_list = [file_dict[i] for i in sorted(file_dict)]
	return sorted_list

def print_csv_lines(root_folder, pattern="_i.csv"):
	filepaths = get_fnames_by_pattern(root_folder, pattern)

	for filepath in filepaths:
		with open(filepath, 'r') as file:
			set_current_sswrp(filepath)
			csv_reader = csv.reader(file)
			for line in csv_reader:
				if validate_tokens(line):
					generate_macros(line)

			tokens = sswrp_str.split('_')
			for token in tokens:
				if token.lower() in dictionary:
					#print("// Token removed from dictionary: ", token.lower())
					dictionary.remove(token.lower())

def print_manual_hacks():
	# If adding lines, add bugs
	manual_hacks = []
	#b/366554453
	manual_hacks.append("#define GSA_GIA_LEVEL_AGGR_1_GSA_GIA_MULTICAST_AGGR_INTR_1       1")
	manual_hacks.append("#define GSA_GIA_LEVEL_AGGR_1_GSA_GIA_PULSE_AGGR_INTR_2           0")
	#b/366531370
	manual_hacks.append("#define DPU_GIA_LEVEL_AGGR_0_AUX_AON_GIA_GIA_GIA_MULTICAST_AGGR_INTR_0       3")
	manual_hacks.append("#define DPU_GIA_LEVEL_AGGR_0_AUX_AON_GIA_GIA_GIA_MULTICAST_AGGR_INTR_1       0")
	manual_hacks.append("#define DPU_GIA_LEVEL_AGGR_0_AUX_AON_GIA_GIA_GIA_MULTICAST_AGGR_INTR_2       1")
	manual_hacks.append("#define DPU_GIA_LEVEL_AGGR_0_AUX_AON_GIA_GIA_GIA_MULTICAST_AGGR_INTR_3       2")

	if len(manual_hacks) == 0:
		return

	print("/*")
	print(" * These lines are manually hacked additions to work around errors in generating from data")
	print(" * add or remove lines from print_manual_hacks() in the generating script")
	print(" */")
	for hack in manual_hacks:
		print(hack)

def print_header():
	now = datetime.datetime.now()
	current_year = now.year

	lines = f"""/* SPDX-License-Identifier: GPL-2.0-only OR BSD-2-Clause */
/*
 * !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
 * !!!!! THIS IS A GENERATED FILE, DO NOT EDIT !!!!!
 * !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
 *
 * This header provides macros for hwirq of MBU GIAs.
 * generate using scripts/interrupt-controller/gen-gia-intr-header.py
 *
 * Copyright {current_year} Google LLC
 */

#ifndef _DT_BINDINGS_IRQ_GIA_GOOGLE_MBU_H
#define _DT_BINDINGS_IRQ_GIA_GOOGLE_MBU_H"""
	print(lines)

def print_footer():
	lines = r"""#endif /* _DT_BINDINGS_IRQ_GIA_GOOGLE_MBU_H */"""
	print(lines)

if __name__ == "__main__":
	if len(sys.argv) != 2:
		print("Usage: python script_name.py <root_folder_path>")
		sys.exit(1)

	root_folder = sys.argv[1]
	print_header()
	print_csv_lines(root_folder)
	print_manual_hacks()
	print_footer()
