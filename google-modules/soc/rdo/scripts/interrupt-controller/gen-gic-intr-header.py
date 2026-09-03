#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
#
# Copyright 2023 Google LLC

"""Generates interrupt header from an interrupt.csv file."""

import argparse
import datetime
import logging
import os
import re
import sys


HDR_HEAD = '\n'.join([
    '/* SPDX-License-Identifier: GPL-2.0-only OR BSD-2-Clause */',
    '/*',
    ' * !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!',
    ' * !!!!! THIS IS A GENERATED FILE, DO NOT EDIT !!!!!',
    ' * !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!',
    ' *',
    ' * This header provides constants for interrupts on {platform_name}.',
    ' * Generate using scripts/interrupt-controller/gen-gic-intr-header.py',
    ' *',
    ' * Copyright {copyright_year} Google LLC',
    ' *',
    ' */',
    '#ifndef _DT_BINDINGS_INTERRUPT_CONTROLLER_{platform_name}_H',
    '#define _DT_BINDINGS_INTERRUPT_CONTROLLER_{platform_name}_H',
    '',
    '#include <dt-bindings/interrupt-controller/arm-gic.h>',
    '',
])


HDR_FOOT = '\n'.join([
    '',
    '#endif  /* _DT_BINDINGS_INTERRUPT_CONTROLLER_{platform_name}_H */',
])

KEY_DSTPORT = 'DstPort'
KEY_SRINST = 'SrcInst'
KEY_IP_NAME = 'INT'

def get_port_from_dstport(dstport, platform_name):
  if platform_name.upper() == 'LGA':
    match_spi = re.match(r'spi_(\d+)_(\d+)\[(\d+)', dstport)
    if match_spi:
      return match_spi.groups()[2]
    else:
      return 'ERROR'

  # MBU logic, which we will assume works for future platforms
  match_spi = re.match(r'cpuacc_cpuacc_intr_spi\[(\d+)', dstport)
  if match_spi:
    return match_spi.groups()[0]

  return 'ERROR'

def main():
  parser = argparse.ArgumentParser()
  parser.add_argument(dest='csv', type=argparse.FileType('r'),
                      help='Path to the interrupt.csv')
  parser.add_argument('-s', '--soc', dest='platform_name', default='MBU',
                      help='Name of the platform')
  parser.add_argument('-c', '--copyright_year', default=datetime.date.today().year,
                      help='Year of the copyright (default: this year)')
  parser.add_argument('-d', dest='debug', action='store_const',
                      const=True, default=False,
                      help='Enable debugging log (default: off)')
  args = parser.parse_args()

  if args.debug:
    logging.getLogger().setLevel(logging.DEBUG)

  logging.debug('%s started with args %s, curdir=%s',
                sys.argv[0], args, os.path.abspath(os.curdir))

  print(HDR_HEAD.format(platform_name=args.platform_name,
                        copyright_year=args.copyright_year))

  int_defs = []
  names = args.csv.readline().split(',')  # row 0th: header
  max_name_len = 0
  for line in args.csv:  # row 1st--last: interrupt descriptions
    interrupts = {names[i]: item
                  for i, item in enumerate(line.split(',')) if i < len(names)}
    dstport = interrupts.get(KEY_DSTPORT, '')

    port = get_port_from_dstport(dstport, args.platform_name)
    if port == 'ERROR':
      continue

    srinst = interrupts.get(KEY_SRINST, None)
    assert srinst, f'cannot find "{KEY_SRINST}" for {interrupts} <- {line}'

    ip_name = interrupts.get(KEY_IP_NAME, None)
    assert ip_name, f'cannot find "{KEY_IP_NAME}" for {interrupts} <- {line}'

    int_name = f'{srinst}_{ip_name}'.upper().removeprefix('U_')
    logging.debug('%s -> %s with IRQ %d', dstport, int_name, int(port))
    int_defs.append((int_name, int(port)))
    max_name_len = max(max_name_len, len(int_name))

  for name, nr in sorted(int_defs, key=lambda x: x[1]):
    spaces = ' ' * (max_name_len - len(name) + 3)
    print(f'#define {name}{spaces}{nr}')

  print(HDR_FOOT.format(platform_name=args.platform_name))
  return 0


if __name__ == '__main__':
  sys.exit(main())
