# SPDX-License-Identifier: GPL-2.0-only
"""Script for verifying pinctrl-mbu.c.

Documentation of script available at go/mbu_pinctrl_validator
"""

import functools
import json
import re
import sys


def extract_sswrp_pins(mbu_file):
  """Extract pin names for each SSWRP from a pinctrl-mbu.c file.

  This function parses the provided pinctrl-mbu.c file and extracts the
  pin names associated with each SSWRP. It uses regular expressions to
  identify the pinctrl_pin_desc definitions and then extracts the pin
  names from the PINCTRL_PIN macros.

  Args:
    mbu_file: The contents of the pinctrl-mbu.c file as a string.

  Returns:
    A dictionary where keys are SSWRP names and values are lists of pin
    names associated with that SSWRP.
  """

  sswrp_pins = {}

  for s in re.findall(
      r"pinctrl_pin_desc google_mbu_(.*?)\[] = \{(.*?)}", mbu_file, re.DOTALL
  ):
    pins = s[1].split("\n\t")
    pins = [re.search(r"PINCTRL_PIN\(.*, \"(.*?)\"\)", p)[1] for p in pins[1:]]
    sswrp_pins[s[0]] = pins

  return sswrp_pins


def extract_pins_functions(mbu_file):
  """Extract pin names and their associated functions from a pinctrl-mbu.c file.

  This function parses the provided pinctrl-mbu.c file and extracts the
  pin names and their corresponding functions from the PIN_GROUP macros.

  Args:
    mbu_file: The contents of the pinctrl-mbu.c file as a string.

  Returns:
    A dictionary where keys are pin names and values are lists of functions
    associated with that pin.
  """
  pins_functions = {}

  for i in re.findall(r"PIN_GROUP\(.*?\),", mbu_file, re.DOTALL):
    tmp = re.sub(r"[\t\n]", "", i)
    tmp = re.findall(r"\((.*)\)", tmp)
    tmp = tmp[0].split(",")
    tmp = [x.strip() for x in tmp]
    pins_functions[tmp[1][1:-1]] = tmp[2:]

  return pins_functions


def increasing(indices):
  for i in range(1, len(indices)):
    if indices[i] < indices[i - 1]:
      return False
  return True


def all_indices_used(indices, n):
  occ = set(indices)

  if len(occ) < n:
    return False

  return all(0 <= x < n for x in indices)


def check_sswrp_pins(pins, regs):
  """Verify the order and usage of pins in a SSWRP.

  This function checks if the pins in the provided `pins` list are used
  consecutively and without any omissions in the `regs` list, which
  represents the register information from the corresponding SSWRP
  JSON file.

  Args:
    pins: A list of pin names associated with the SSWRP.
    regs: A list of dictionaries representing the registers in the SSWRP.

  Returns:
    True if the pins are used in the correct order and without omissions,
    False otherwise. Prints error messages to stdout if any issues are
    detected.
  """
  indices = []

  for r in regs:
    matches = []
    for i, p in enumerate(pins):
      if r["name"].startswith(p):
        matches.append((len(p), i))

    matches.sort(reverse=True)
    indices.append(matches[0][1])

  if not increasing(indices):
    print(f"Pins not in increasing order: {regs}")
    return False

  if not all_indices_used(indices, len(pins)):
    print(f"Pins not used: {regs}")
    return False

  return True


def custom_get_func(val, el):
  """Recursively retrieve a value from a nested dictionary or list.

  This function traverses a nested dictionary or list structure to find
  and return the value associated with the specified key `el`. It supports
  both direct key access in dictionaries and searching for a dictionary
  with a matching "name" key within a list.

  Args:
    val: The nested dictionary or list to search within.
    el: The key or "name" value to search for.

  Returns:
    The value associated with the key `el` if found, otherwise raises a
    TypeError.

  Raises:
    TypeError: If the input `val` is not a dictionary or list, or if the
      key `el` is not found.
  """
  if isinstance(val, dict):
    return val[el]
  elif isinstance(val, list):
    for l in val:
      if isinstance(l, dict) and l["name"] == el:
        return l
  else:
    raise TypeError(f"Unexpected type: {val}")


def unique_functions(pin_functions):
  for pin in pin_functions:
    functions = pin_functions[pin]
    for f in functions:
      if f != "_" and functions.count(f) > 1:
        print(f"Identical functions found for pin: {pin}")
        return False
  return True


def all_gpio_on_first(pin_functions):
  for pin in pin_functions:
    functions = pin_functions[pin]
    for i, f in enumerate(functions):
      if "gpio" in f and i != 0:
        print(f"GPIO function found on non-first position for pin: {pin}")
        return False
  return True


def check_pin_functions(pin_functions):
  return unique_functions(pin_functions) and all_gpio_on_first(pin_functions)


def check_sswrp(sswrp, path, sswrp_pins, pins_functions):
  """Verify the pin usage and function configuration for a given SSWRP.

  This function performs a series of checks to ensure the correctness of
  the pinctrl-mbu.c file against the corresponding SSWRP JSON file. It
  verifies the order and usage of pins within the SSWRP, as well as the
  uniqueness and ordering of functions associated with each pin.

  Args:
    sswrp: The name of the SSWRP to be checked.
    path: A list of keys representing the path to the SSWRP block within the
      JSON file.
    sswrp_pins: A dictionary mapping SSWRP names to lists of pin names.
    pins_functions: A dictionary mapping pin names to lists of functions.

  Returns:
    None. The function exits the program with code 1 if any errors are
    detected.
  """
  with open(f"si_codex/sswrp_{sswrp}.json") as f:
    sswrp_json = json.load(f)

  block = functools.reduce(custom_get_func, path, sswrp_json)

  if not check_sswrp_pins(sswrp_pins[sswrp], block["reg"]):
    sys.exit(1)

  if not check_pin_functions(pins_functions):
    sys.exit(1)


def main():
  """Verify the pin usage and function configuration for all SSWRPs.

  This function reads the pinctrl-mbu.c file and extracts pin names and
  functions. It then iterates through a list of SSWRP names and their
  corresponding paths within the JSON files. For each SSWRP, it calls
  the `check_sswrp` function to verify the pin usage and function
  configuration against the corresponding JSON file.

  The function exits the program with code 1 if any errors are detected
  during the verification process.
  """
  with open("../pinctrl-mbu.c") as f:
    mbu_file = f.read()

    sswrp_pins = extract_sswrp_pins(mbu_file)
    pins_functions = extract_pins_functions(mbu_file)

  sswrps = [
      ("pcie", ["chip", "block", "PCIE_BK_csr"]),
      ("hsios", ["chip", "block", "HSIOS_BK_csr"]),
      ("hsios_stby", ["chip", "block", "HSIOS_STBY_BK_csr"]),
      ("aoss", ["chip", "chip", "SSWRP_AOSS_AON", "block", "AOSS_BK_csr"]),
      ("gdmc", ["chip", "block", "gpio_csrs"]),
      ("lsios", ["chip", "block", "LSIOS_BK_csr"]),
      ("lsioe", ["chip", "block", "LSIOE_BK_csr"]),
  ]

  for sswrp, path in sswrps:
    check_sswrp(sswrp, path, sswrp_pins, pins_functions)


if __name__ == "__main__":
  main()
