"""Tooling to read/parse AMS-related data from system files in /proc and /sys.

The data can be read by calling gather_all_data() and is returned as a 3-tuple of
dictionaries for system data, bus data, and data for devices.
"""

__author__ = "Robert Jennings rcj@linux.vnet.ibm.com"
__copyright__ = "Copyright (c) 2008 IBM Corporation"
__license__ = "Common Public License v1.0"

__version__ = "$Revision: 1.5 $"
__date__ = "$Date: 2009/01/20 21:28:29 $"
# $Source: /cvsroot/powerpc-utils/powerpc-utils-papr/scripts/amsvis/powerpcAMS/amsdata.py,v $

import os
import sys
import logging
import types

def _read_file_for_data(filename, params, split_char=':', rstrip="",
                        input_dict={}):
    """Read a single file to pull a number of values for variables.

    All of the values read from the file are stored in the output dictionary
    as types.IntType.

    Keyword arguments:
    filename -- File to read for data.
    params -- Dictionary where the keys are strings to match in the file and
        values are the keys to use in the output dictionary.
    split_char -- Character to delineate the key from the value in the file.
    rstrip -- String to strip off of the right side of the value found.
    input_dict -- Dictionary to add/replace values in

    Returns:
    Dictionary of values based on keys from 'params' dictionary, with all
        values stored as type.IntType.
    None on error.
    """
    vals = input_dict
    try:
        file = open(filename)
        for line in file:
            (var, sep, val) = line.partition(split_char)
            if var in params:
                vals[params[var]] = int(val.strip().rstrip(rstrip))
        file.close()
    except (IOError, OSError), data:
        logging.error("Error encountered while collecting data:")
        logging.error("  " + str(data))
        return None
    return vals

def _read_file_as_data(file):
    """Read a file for the single value it contains and return that value as
    a types.IntType.

    Keyword arguments:
    file - File to read for data.

    Returns:
    Value from file as types.IntType or None on error.
    """
    file = open(file)
    value = int(file.readline().strip())
    file.close()
    return value


def _get_system_data():
    """Capture system memory usage statistics from a number of sources."""
    meminfo_file = "/proc/meminfo"
    meminfo_params = {"MemTotal" : "memtotal",
                      "MemFree"  : "memfree",
                      "Buffers"  : "buffers",
                      "Cached"   : "cached"}
    lparcfg_file = "/proc/ppc64/lparcfg"
    lparcfg_params = {"cmo_faults" : "faults",
                      "cmo_fault_time_usec" : "faulttime"}
    cmm_path = "/sys/devices/system/cmm/cmm0/"
    cmm_params = {"loaned_kb":"memloaned"}
    sys_data = {}

    sys_data = _read_file_for_data(meminfo_file , meminfo_params, ':',
                                   rstrip=" kB")

    if sys_data is None:
        logging.error("Unable to gather system data (meminfo).")
        return None

    sys_data = _read_file_for_data(lparcfg_file, lparcfg_params, '=',
                                    input_dict = sys_data)

    if sys_data is None:
        logging.error("Unable to gather system data (lparcfg).")
        return None

    sys_data["memused"] = (sys_data["memtotal"] - sys_data["memfree"] -
                           sys_data["buffers"] - sys_data["cached"])

    for param in cmm_params:
        try:
            sys_data[cmm_params[param]] = _read_file_as_data(os.path.join(cmm_path, param))
        except (IOError, OSError):
            sys_data[cmm_params[param]] = None

    if sys_data["memloaned"] is not None:
        sys_data["memtotal"] += sys_data["memloaned"]

    return sys_data

def _get_bus_data():
    """Capture VIO bus data from sysfs regarding memory entitlement."""
    bus_path = "/sys/bus/vio/"
    bus_params = {"cmo_entitled"     : "entitled",
                  "cmo_min"          : "min",
                  "cmo_desired"      : "desired",
                  "cmo_curr"         : "curr",
                  "cmo_reserve_size" : "reserve",
                  "cmo_excess_size"  : "excess",
                  "cmo_excess_free"  : "excessfree",
                  "cmo_high"         : "high",
                  "cmo_spare"        : "spare"}
    bus_data = {}

    try:
        for param in bus_params:
            bus_data[bus_params[param]] = _read_file_as_data(os.path.join(bus_path, param))
    except (IOError, OSError), data:
        logging.error("Error encountered while collecting AMS bus data:")
        logging.error("  " + str(data))
        logging.error("Is the kernel enabled for this feature?")
        return None

    # Convert bytes to Kb for bus data to match system variables
    for key in bus_data:
        bus_data[key] = int(bus_data[key] / 1024)

    return bus_data

def _get_device_data():
    """Capture data for devices on the VIO bus regarding memory entitlement.

    Returns:
        Dictionary with data where each key is a device names and value is a
        dictionary of the values for that device.
        Returns None on error.
    """
    dev_path = "/sys/bus/vio/devices/"
    dev_params = {"cmo_desired"      : "desired",
        "cmo_entitled"     : "entitled",
        "cmo_allocated"    : "allocated",
        "cmo_allocs_failed": "allocs_failed"}
    devices = {}

    # Assemble a list of devices from the directories present in sysfs
    try:
        dev_dirs = [os.path.join(dev_path, entry) \
            for entry in os.listdir(dev_path)     \
            if (entry != "vio" and
                os.path.isdir(os.path.join(dev_path, entry)))]
    except (IOError, OSError), data:
        logging.error("Error encountered while discovering devices on the virtual IO bus:")
        logging.error("  " + str(data))
        logging.error("Does this system have virtual IO devices or VIO bus support?")
        return None

    # Collect data for each device
    for dir in dev_dirs:
        dev_vals = {}
        try:
            name = dir.replace(dev_path, "")
            dev_vals["name"] = name
            for param in dev_params:
                dev_vals[dev_params[param]] = _read_file_as_data(os.path.join(dir, param))
        except (IOError, OSError):
            # In the case that there is a problem we remove the device
            # by setting the entitlement to 0
            dev_vals["entitled"] = 0

        if dev_vals["entitled"] != 0:
            devices[name] = dev_vals

    # Convert bytes to Kb for device data to match system variables
    dev_params.pop("cmo_allocs_failed")
    for dev in devices:
        for key in devices[dev]:
            if (key in dev_params.values() and
                type(devices[dev][key]) is types.IntType):
                devices[dev][key] = int(devices[dev][key] / 1024)

    return devices

def gather_all_data():
    """ Gather all data from the OS for AMS and assemble.

    Returns:
        3-tuple of dictionaries for system, bus, and devices data.
        None returned on error.
    """
    sys_data = _get_system_data()
    if sys_data is None:
        return None

    bus_data = _get_bus_data()
    if bus_data is None:
        return None

    device_data = _get_device_data()
    if device_data is None:
        return None

    if sys_data is None or bus_data is None or device_data is None:
        return None

    # Devices aren't complete without providing the amount of excess pool
    # used as calculated from the bus
    excess_used = bus_data["excess"] - bus_data["excessfree"]
    for dev in device_data:
        device_data[dev]["excess_used"] = excess_used
        device_data[dev]["maxavail"] = (bus_data["excess"] +
                                        device_data[dev]["entitled"])

    return sys_data, bus_data, device_data

def gather_system_data():
    """ Gather only system data from the OS for AMS and assemble.
    The data is restricted to what is collected by _get_system_data().

    Returns:
        Dictionary for system data.
        None returned on error.
    """
    return _get_system_data()
