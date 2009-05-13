"""Network client/server for transmitting pickled data.
"""

__author__ = "Robert Jennings rcj@linux.vnet.ibm.com"
__copyright__ = "Copyright (c) 2008 IBM Corporation"
__license__ = "Common Public License v1.0"

__version__ = "$Revision: 1.3 $"
__date__ = "$Date: 2009/01/16 16:39:10 $"
# $Source: /cvsroot/powerpc-utils/powerpc-utils-papr/scripts/amsvis/powerpcAMS/amsnet.py,v $

import socket
import sys
import os
import types
import logging
import cPickle as pickle
from optparse import OptionParser

from powerpcAMS.amsdata import *

cmd_GET_ALL_DATA = 0
cmd_GET_SYS_DATA = 1
cmd_MAX = 1
data_methods = (gather_all_data, gather_system_data)

# Update cmdvers each time a new method is added to data_methods.
# The update should increment the digits to the right of the decimal point.
# The digits to the left of the decimal point should be increased when
# backwards compatibility is broken.
cmdvers = 1.0000000

def send_data_loop(port):
    """Send pickled data to any client that connects to a given network port.

    Keyword arguments:
    port -- network port number to use for this server
    """

    sock = socket.socket(socket.AF_INET,socket.SOCK_STREAM);
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    try:
        sock.bind(('', port))
    except socket.error, msg:
        (errno, errstr) = msg.args
        logging.error("Network error: (%d) " % errno + errstr)
        return 1

    sockfile = None
    conn = None

    try:
        while (1):
            sock.listen(1)
            (conn, addr) = sock.accept()
            logging.debug("Client connected from " + repr(addr))
            sockfile = conn.makefile('rwb',0)

            result = "success"
            data = None
            client_data = None
            # Read request from client
            # By accepting a request from the client, including the
            # request data version we can change what the server will
            # send in the future.
            try:
                client_data = pickle.Unpickler(sockfile).load()
            except:
                logging.debug("Unable to parse client request.")
                logging.info("Bad client request, ignoring.")
                result = "error"
                data = "bad client request"

            # Currently the server only expects a dictionary from the client
            # with the following values to send AMS data:
            # {"command":0, "version":1.0}
            if (result is not "error" and 
                 ("version" not in client_data or
                  client_data["version"] > cmdvers or
                  int(client_data["version"]) != int(cmdvers))):
                logging.debug("Unsupported client request version, ignoring.")
                result = "error"
                data = "Unsupported version, server is %f" % cmdvers

            if (result is not "error" and
                 ("command" not in client_data or
                  client_data["command"] < 0 or
                  client_data["command"] > cmd_MAX)):
                logging.debug("Unsupported request from client, ignoring.")
                result = "error"
                data = "Unsupported request"

            if result is not "error":
                data_method = data_methods[client_data["command"]]

                # Gather system data and send pickled objects to the client
                data = data_method()
                if data is None:
                    result = "error"
                    data = "Unspecified data gathering error, check server log."
                logging.debug("Sending %d data objects to client." % len(data))

            response = {"result":result, "data":data}
            sockfile.writelines(pickle.dumps(response, -1))

            # Clean up
            sockfile.close()
            sockfile = None

            conn.close()
            conn = None

    # Catch a keyboard interrupt by cleaning up the socket
    except KeyboardInterrupt:
        if sockfile:
            sockfile.close()
        if conn:
            conn.close()
        sock.close()
        logging.info("Server exiting due to keyboard interrupt.")
        return 0

    # Catch a network error and clean up, return 1 to indicate an error
    except socket.error, msg:
        if sockfile:
            sockfile.close()
        if conn:
            conn.close()
        sock.close()
        (errno, errstr) = msg.args
        logging.error("Network error: (%d) " % errno + errstr)
        return 1

    # Give the user something slightly helpful for any other error
    except:
        if sockfile:
            sockfile.close()
        if conn:
            conn.close()
        sock.close()
        logging.error("Unknown error while sending data.")
        raise

# Client
def net_get_data(host="localhost", port=50000, cmd=cmd_GET_ALL_DATA):
    """Get pickled data from a simple network server.

    Keywork arguments:
    host -- server host name (default localhost)
    port -- server port number (default 50000)

    Returns:
    List of objects received from the server
    """
    data = {}

    sock = socket.socket(socket.AF_INET,socket.SOCK_STREAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    try:
        sock.connect((host,port))
    except socket.error, msg:
        (errno, errstr) = msg.args
        logging.error("Network error: (%d) " % errno + errstr)
        return {"result":"error","data":"Client: Is the server still running?"}

    sockfile = sock.makefile('rwb',0) # read/write, unbuffered

    # By sending a request to the server, including a version for the data
    # request, we can change the data sent by the server in the future.
    if type(cmd) is types.IntType and cmd >= 0 and cmd <= cmd_MAX:
        client_request = {"command":cmd, "version":cmdvers}
        logging.debug("Sending request for %s (ver:%f)" %
                      (client_request["command"], client_request["version"]))
    else:
        logging.error("BUG: Unknown command request for network client.")
        print type(cmd)
        print repr(cmd)
        return {"result":"error","data":"Client: Bad request."}

    sockfile.writelines(pickle.dumps(client_request, -1))

    # Get server response
    pickler = pickle.Unpickler(sockfile)
    try:
        data = pickler.load()
    except EOFError:
        pass
    sockfile.close()
    sock.close()

    if type(data) is not types.DictType or "result" not in data:
        data = {"result":"error", "data":"Unknown server error"}

    logging.debug("Data returned to client: " + repr(data))

    return data
