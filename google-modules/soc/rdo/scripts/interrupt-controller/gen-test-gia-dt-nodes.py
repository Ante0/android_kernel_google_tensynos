#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
#
# Copyright 2024 Google LLC

import argparse
import datetime
import re

test_line = 31
test_line_name = "TEST_LINE_31"

lga_sswrps_with_ier = [
    'eh',
    'gpdma',
    'hsion',
    'hsios',
    'inftcu',
    'ispbe',
    'fabhbw',
    'fabmed',
    'fabrt',
    'fabsyss',
    'fabstby',
    'cpu',
    'gsa',
]

sswrp_mapping = {
    'hsion': 'HSIO_N',
    'hsios': 'HSIO_S',
    'inftcu': 'INF_TCU',
}

global_soc = ''

def parse_macros(macros_file):
    macros = {}
    with open(macros_file, 'r') as file:
        for line in file:
            if line.startswith("#define"):
                match = re.match(r'#define\s+(\w+)\s+(\d+)', line)
                if match:
                    macro_name, interrupt_line = match.groups()
                    macros[macro_name] = int(interrupt_line)
    return macros


def build_interrupts_from_macros(node_name, macros):
    interrupt_numbers = []
    interrupt_names = []

    # SSWRP, TYPE, _AGGR_, NUMBER
    node_name_bits = node_name.split('_')
    # handle hsio_s and hsio_n

    # SPECIAL SSWRP HANDLING:
    # node_name_bits[0] is SSWRP
    sswrp = node_name_bits[0]
    # second parameter is default value: If we don't get a translation, use what we already had
    node_name_bits[0] = sswrp_mapping.get(sswrp, sswrp)

    node_name_bits.insert(1,"GIA")

    # discard anything after number
    macro_prefix = '_'.join(node_name_bits[:5]).upper() + '_'

    # assumption: Macros are in order by interrupt number per GIA device
    for macro_name in macros:
        if macro_name.startswith(macro_prefix):
            interrupt_numbers.append(f"\n\t\t\t<{macro_name}>")
            interrupt_names.append(f"\n\t\t\t\"{macro_name}\"")

    # not all SSWRPS in LGA let us correctly insert a test line.
    ier_enabled = global_soc != 'lga' or bool(sswrp in lga_sswrps_with_ier)

    # only insert test lines on nodes which are not fully claimed by real irqs
    # =0 case is for nodes without data in interrupts tracker (ISPBE). Add only test line.
    # There could be an interrupt on line 31 and the other lines are not all taken. This is ignored
    # Note also that we do not provide test lines for wide GIAs
    has_room_for_test_line = len(interrupt_numbers) >= 0 and len(interrupt_numbers) < 32

    # TODO: redirect rejects to another file, add test line
    if ier_enabled and has_room_for_test_line:
            interrupt_numbers.append(f"\n\t\t\t<{test_line}>")
            interrupt_names.append(f"\n\t\t\t\"{test_line_name}\"")

    return (interrupt_numbers, interrupt_names)


def process_device_tree(device_tree_file, out_file, macros):
    with open(device_tree_file, 'r') as in_file:
        current_node_lines = []  # Store lines for the current node

        for line in in_file:
            # Check if starting a new node
            # ISPBE nodes are badly named. Not processing name hard here as we will validate inside
            # process_node().
            match = re.search(r'^\s*(\w+):.*{', line)

            if match:
                # Process previous node's lines (if any)
                # not checking for }; to end node because I want the first line of the node to be
                # the name for easier checking inside process_node(). Could be changed to ignore
                # whitespace-only lines and use }; instead.
                if current_node_lines:
                    process_node(current_node_lines, out_file, macros)
                    current_node_lines = []  # Reset for the new node

            current_node_lines.append(line.strip())  # Add line to the current node

        # Process the last node
        if current_node_lines:
            process_node(current_node_lines, out_file, macros)


def process_node(node_lines, out_file, macros):
    match = re.search(r'^\s*(\w+_aggr_\w+):.*{', node_lines[0])
    # does not match ISPBE - pending ISPBE rename. use below line to match ispbe
    # match = re.search(r'^\s*(\w+):.*{', node_lines[0])
    if not match:
        print(f"Warning: Skipping node with bad format: {node_lines[0]}")
        return

    node_name = match.group(1)

    new_node_name = f"{node_name}_test: {node_name}_test"

    new_node = f"\t{new_node_name} {{\n"
    new_node += "\t\tcompatible = \"google,gia-test\";\n"

    # Copy power-domains
    for line in node_lines:
        match = re.search(r'power-domains\s*=\s*(.*);', line)
        if match:
            new_node += f"\t\t{match.group(0)}\n"
            break  # Stop after finding the first power-domains line

    new_node += f"\t\tinterrupt-parent = <&{node_name}>;\n"

    (intr_numbers, intr_strs) = build_interrupts_from_macros(node_name, macros)

    # If we got one, we got both.
    # If We got neither, don't print this one in the file
    if not intr_numbers:
        print(f"Did not find interrupt macros for {node_name}")
        return

    new_node += f"\t\tinterrupts ={','.join(intr_numbers)};\n"
    new_node += f"\t\tinterrupt-names ={','.join(intr_strs)};\n"

    new_node += "\t};\n\n"

    out_file.write(new_node)


def print_header(out_file):
    now = datetime.datetime.now()
    current_year = now.year

    lines = f"""// SPDX-License-Identifier: GPL-2.0-only OR BSD-3-Clause
/*
 * !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
 * !!!!! THIS IS A GENERATED FILE, DO NOT EDIT !!!!!
 * !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
 *
 * This file provides test nodes for use with the GIA test driver
 * Generate using scripts/irqchip/gen-test-gia-dt-nodes.py
 *
 * Copyright {current_year} Google LLC
 */

#include <dt-bindings/interrupt-controller/{global_soc}-gic.h>
#include <dt-bindings/interrupt-controller/irq-gia-google-{global_soc}.h>

&{{/}} {{
"""

    out_file.write(lines)

def print_footer(out_file):
    lines = "};"
    out_file.write(lines)

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('-d', '--device-tree', dest='device_tree_file',
				required=True, help='GIA device tree file')
    parser.add_argument('-o', '--output-file', dest='output_file',
				required=True, help='Path to the base addrs per sswrp csv')
    parser.add_argument('-m', '--macros-file', dest='macros_file',
				required=True, help='GIA macros file generated by gen-gia-intr-header.py')
    parser.add_argument('-s', '--soc', dest='soc', default='lga',
                help='soc to generate test DT nodes for')
    args = parser.parse_args()

    global global_soc
    global_soc = args.soc.lower()

    # should macros be global? It gets passed around a lot
    # Similar q for out_file.
    macros = parse_macros(args.macros_file)

    with(open(args.output_file, 'w') as out_file):
        print_header(out_file)
        process_device_tree(args.device_tree_file, out_file, macros)
        print_footer(out_file)

if __name__ == "__main__":
	main()
