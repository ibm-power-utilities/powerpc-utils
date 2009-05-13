"""bar.py test script for drawing a bar readout
"""

__author__ = "Robert Jennings rcj@linux.vnet.ibm.com"
__copyright__ = "Copyright (c) 2008 IBM Corporation"
__license__ = "Common Public License v1.0"

__version__ = "$Revision: 1.6 $"
__date__ = "$Date: 2009/01/21 15:38:20 $"
# $Source: /cvsroot/powerpc-utils/powerpc-utils-papr/scripts/amsvis/powerpcAMS/amswidget.py,v $

import gtk
from gtk import gdk
import cairo
import math
import types

class ams_widget(gtk.DrawingArea):
    """Memory metrics widget based on a gtk.DrawingArea

    This widget should be used directly, it is provided as a base
    class for subclassing widgets to display specific AMS memory metrics."""
    def __init__(self, data={}):
        super(ams_widget, self).__init__()

        # gtk.Widget signals
        self.connect("expose_event", self.expose)

        # Private data
        self.hist_size = 12
        self.data = []
        self.data.insert(0, data.copy())

        # The layout of this widget includes a 5 pixel border
        # ---------------------
        # |         |         |
        # |         |         |
        # |         |         |
        # | Title   |  Bar    |
        # |         |         |
        # |         |         |
        # |         |         |
        # |-------------------|
        # |                   |
        # |       Data        |
        # |                   |
        # ---------------------
        #
        # The 'Title' and 'Data' areas are of fixed size, the bar size is
        # dynamic.
        self.settings = {}
        self.settings["border"] = 5
        self.settings["title_width"] = 100
        self.settings["title_height"] = 200
        self.settings["data_width"] = 200
        self.settings["data_height"] = 110
        self.settings["data_spacing"] = 3
        self.settings["bar_min_width"] = 100
        self.settings["bar_min_height"] = 200
        self.settings["legend_spacing"] = 5

        min_width = max((self.settings["title_width"] +
                         self.settings["bar_min_width"]),
                        self.settings["data_width"])
        min_height = max(self.settings["title_height"],
                         (self.settings["bar_min_height"] +
                          self.settings["data_height"]))
        self.set_size_request((min_width + (self.settings["border"] * 2)),
                              (min_height + (self.settings["border"] * 2)))

        # Graphics colors (used for drawing the objects and the legend)
        self.settings["widget_bg"] = {"r":0.1, "g":0.1, "b":0.1}
        self.settings["bar_bg"] = {"r":1.0, "g":1.0, "b":1.0}
        self.settings["title_rgba"] = {"r":0.9, "g":0.9, "b":0.9, "a":1.0}
        self.settings["text_rgba"] = {"r":0.9, "g":0.9, "b":0.9, "a":1.0}

        # Font variables
        self.settings["title_font_name"] = "Courier New"
        self.settings["title_font_size"] = 12
        self.settings["label_font_name"] = "Courier New"
        self.settings["label_font_size"] = 12
        self.settings["label_height"] = 10

    def expose(self, widget, event):
        context = widget.window.cairo_create()
        context.rectangle(event.area.x, event.area.y, event.area.width,
                          event.area.height)
        context.clip()
        self.draw(context)
        return False

    def draw_rounded_rectangle(self, cr, x, y, width, height, corner_size=4):
        """Draw a rectangle with rounded corners.

        This function completes the path for a rectangle with rounded
        corners for a specified context but does not stroke or fill the
        shape.

        Keyword arguments:
        cr -- graphics context
        x, y -- starting coordinate for rectangle
        width, height -- size of rectangle to draw
        corner_size -- radius of arc that will define the corners of this
            rectangle
        """
        x0 = x
        x1 = x + width
        y0 = y
        y1 = y0 + height
        size = corner_size
        cr.arc((x0 + size), (y0 + size), size, -math.pi, (-math.pi / 2))
        cr.line_to((x1 - size), y0)
        cr.arc((x1 - size), (y0 + size), size, (-math.pi / 2), 0)
        cr.line_to(x1, (y1 - size))
        cr.arc((x1 - size), (y1 - size), size, 0, (math.pi / 2))
        cr.line_to((x0 + size), y1)
        cr.arc((x0 + size), (y1 - size), size, (math.pi / 2), math.pi)
        cr.line_to(x0, (y0 + size))
        cr.close_path()

    def draw_setup(self, cr):
        """Set up the graphics context to draw the widget

        Sets widgets size request and then based on the allocation provided,
        defines the size of the bar drawing area.  The background is also
        painted at this time.

        Keyword arguments:
        cr -- graphics context
        """
        # Set widget size
#        rect = self.get_allocation()
#        min_width = max((self.settings["title_width"] +
#                         self.settings["bar_min_width"]),
#                        self.settings["data_width"])
#        min_height = max(self.settings["title_height"],
#                         (self.settings["bar_min_height"] +
#                          self.settings["data_height"]))
#        self.set_size_request((min_width + (self.settings["border"] * 2)),
#                              (min_height + (self.settings["border"] * 2)))
        rect = self.get_allocation()
        self.settings["bar_width"] = rect.width - self.settings["title_width"]
        self.settings["bar_height"] = rect.height - self.settings["data_height"]

        # Color the background dark grey
        cr.set_source_rgb(self.settings["widget_bg"]["r"],
                          self.settings["widget_bg"]["g"],
                          self.settings["widget_bg"]["b"])
        cr.paint()

    def draw_title(self, cr, title):
        """Draw a title within the widget.

        The title is located in the upper left of the widget.

        Keyword arguments:
        cr -- graphics context
        title -- a string containing the title to be used
        """
        cr.set_source_rgb(self.settings["text_rgba"]["r"],
                          self.settings["text_rgba"]["g"],
                          self.settings["text_rgba"]["b"])
        cr.select_font_face(self.settings["title_font_name"],
                            cairo.FONT_SLANT_NORMAL,
                            cairo.FONT_WEIGHT_BOLD)
        cr.set_font_size(self.settings["title_font_size"])
        height = cr.text_extents(title)[3]
        cr.move_to(self.settings["border"], (self.settings["border"] + height))
        cr.show_text(title)

    def draw_bar_bg(self, cr):
        """Draw a gradient background for the bar graph.

        The bar background is a linear gradient.  Graph elements are intended
        to be drawn on this background using an alpha channel.

        Keyword arguments:
        cr -- graphics context
        """
        # Draw bar background
        linear = cairo.LinearGradient(0, 0, 0.5, 0)
        linear.add_color_stop_rgb(0.0, self.settings["bar_bg"]["r"],
                                  self.settings["bar_bg"]["g"],
                                  self.settings["bar_bg"]["b"])
        linear.add_color_stop_rgb(1.0,  (self.settings["bar_bg"]["r"] * 0.8),
                                  (self.settings["bar_bg"]["g"] * 0.8),
                                  (self.settings["bar_bg"]["b"] * 0.8))
        cr.set_source(linear)
        cr.rectangle(0, 0, 0.5, 1)
        cr.fill()

    def draw_labels_setup(self, cr):
        """Set text characteristics and move to the correct position prior to
        drawing labels.

        Keyword arguments:
        cr -- graphics context
        """
        cr.set_source_rgb(self.settings["text_rgba"]["r"],
                          self.settings["text_rgba"]["g"],
                          self.settings["text_rgba"]["b"])
        cr.select_font_face(self.settings["label_font_name"],
                            cairo.FONT_SLANT_NORMAL,
                            cairo.FONT_WEIGHT_BOLD)
        cr.set_font_size(14)
        cr.translate(0, (self.settings["bar_height"] +
                         self.settings["data_spacing"]))

    def draw_label(self, cr, label, value):
        """Draw a label in the 'data' area of the widget

        Keyword arguments:
        cr -- graphics context
        label -- text label for the data, if specified, to be displayed on
            the left of the 'data' area
        value -- value of the data, if specified, to be displayed on the
            right side of the 'data' area.
        """
        if value is not None:
            data_width = cr.text_extents(str(value))[2]
            cr.move_to((self.get_allocation().width -
                       (self.settings["border"] * 2) - data_width),
                       self.settings["label_height"])
            cr.show_text(str(value))
        if label is not None:
            cr.move_to(self.settings["border"], self.settings["label_height"])
            cr.show_text(label)
        cr.translate(0, (self.settings["label_height"] +
                         self.settings["data_spacing"]))

    def draw_label_and_legend(self, cr, label, value, legend):
        """Draw a label in the 'data' area of the widget with a legend color.

        Keyword arguments:
        cr -- graphics context
        label -- text label for the data, if specified, to be displayed on
            the left of the 'data' area
        value -- value of the data, if specified, to be displayed on the
            right side of the 'data' area.
        legend -- color to use for the legend
        """
        if (value != None):
            data_width = cr.text_extents("%d" % value)[2]
            cr.move_to((self.get_allocation().width -
                        (self.settings["border"] * 2) - data_width),
                       self.settings["label_height"])
            cr.show_text("%d" % value)

        if (label != None):
            cr.save()
            cr.set_source_rgb(legend["r"], legend["g"], legend["b"])
            cr.move_to(self.settings["border"], self.settings["label_height"])
            cr.arc((self.settings["legend_spacing"]),
                   (self.settings["legend_spacing"] + 1),
                   (self.settings["legend_spacing"] - 1), 0, math.pi * 2)
            cr.close_path()
            cr.fill()
            cr.restore()
            cr.move_to((self.settings["border"] +
                        self.settings["legend_spacing"]),
                       self.settings["label_height"])
            cr.show_text(label)
        cr.translate(0, (self.settings["label_height"] +
                         self.settings["data_spacing"]))

    def draw(self, cr):
        return

    def redraw_canvas(self):
        if self.window:
            alloc = self.get_allocation()
            self.queue_draw_area(0, 0, alloc.width, alloc.height)
            self.window.process_updates(True)

    def update_values(self, values):
        """Update the system AMS values that the widget will draw.

        The widget is redrawn after the udpate completes

        Keyword arguments:
        values -- a dictionary of data for the widget
        """
        self.data.insert(0, values.copy())
        if len(self.data) > self.hist_size:
            for x in range((len(self.data) - self.hist_size)):
                self.data.pop()
        self.redraw_canvas()

    def update_settings(self, settings):
        """Update graphics variables used to draw the widget.

        The widget is redrawn if values are updated.

        Keyword arguments:
        values -- a dictionary where the keys match keys present in the
            widget's graphics related dictionary.
        """
        update = False
        for key in self.settings:
            if key in settings:
                self.settings[key] = settings[key]
                update = True
        if update is True:
            self.redraw_canvas()

class system_name_widget(ams_widget):
    """Widget to display the name of a system based on a gtk.DrawingArea"""
    def __init__(self, hostname=None):
        super(system_name_widget, self).__init__()

        self.hostname = hostname

        # Widget geometry data
        self.settings["title_width"] = 200
        self.settings["title_height"] = 15
        self.settings["data_width"] = 0
        self.settings["data_height"] = 0
        self.settings["data_spacing"] = 0
        self.settings["bar_min_width"] = 0
        self.settings["bar_min_height"] = 0

        self.set_size_request((self.settings["title_width"] +
                               (self.settings["border"] * 2)),
                              (self.settings["title_height"] +
                               (self.settings["border"])))

    def expose(self, widget, event):
        context = widget.window.cairo_create()
        # Set a clip region for the expose event
        context.rectangle(event.area.x, event.area.y, event.area.width,
                          event.area.height)
        context.clip()
        self.draw(context)
        return False

    def draw(self, cr):
        self.draw_setup(cr)
        self.draw_title(cr, self.hostname)

    def redraw_canvas(self):
        if self.window:
            alloc = self.get_allocation()
            self.queue_draw_area(0, 0, alloc.width, alloc.height)
            self.window.process_updates(True)


class system_memory_widget(ams_widget):
    """Widget to display system memory metrics based on a gtk.DrawingArea"""
    def __init__(self, data={}):
        super(system_memory_widget, self).__init__(data)

        # Graphics colors (used for drawing the objects and the legend)
        self.settings["loaned_rgba"] = {"r":0.4, "g":0.4, "b":0.4, "a":0.9}
        self.settings["used_rgba"] = {"r":0.2, "g":0.8, "b":0.2, "a":0.6}
        self.settings["used_other_rgba"] = {"r":0.2, "g":0.8, "b":0.2, "a":0.4}

    def expose(self, widget, event):
        context = widget.window.cairo_create()
        context.rectangle(event.area.x, event.area.y, event.area.width,
                          event.area.height)
        context.clip()
        self.draw(context)
        return False

    def draw(self, cr):
        self.draw_setup(cr)
        self.draw_title(cr, "System")

        # Save graphics context
        cr.save()

        # Set the coordinates and scale
        cr.move_to(0,0)
        cr.translate((self.settings["border"] + self.settings["title_width"]),
                     self.settings["border"])
        cr.scale(((self.settings["bar_width"] * 2) -
                  (self.settings["border"] * 4)),
                 (self.settings["bar_height"] - self.settings["border"]))

        # Create a grouping
        cr.push_group()
        self.draw_bar_bg(cr)

        bar_width = 0.5 / self.hist_size
        bar_x = 0.5 - bar_width
        bar_overlap = 0.005
        for iter in range(len(self.data)):
            # Create a types.FloatType representation of memtotal so that
            # division operations used for drawing are not cast to types.IntType
            fl_memtotal = float(self.data[iter]["memtotal"])

            # Draw used from bottom
            used_height = self.data[iter]["memused"] / fl_memtotal
            used_y = 1 - used_height
            cr.set_source_rgba(self.settings["used_rgba"]["r"],
                               self.settings["used_rgba"]["g"],
                               self.settings["used_rgba"]["b"],
                               self.settings["used_rgba"]["a"])
            cr.rectangle(bar_x, used_y, (bar_width + bar_overlap), used_height)
            cr.fill()

            # Draw used (buffers and cache) next
            used_other_height = ((self.data[iter]["buffers"] +
                                  self.data[iter]["cached"]) /
                                 fl_memtotal)
            used_other_y = used_y - used_other_height
            cr.set_source_rgba(self.settings["used_other_rgba"]["r"],
                               self.settings["used_other_rgba"]["g"],
                               self.settings["used_other_rgba"]["b"],
                               self.settings["used_other_rgba"]["a"])
            cr.rectangle(bar_x, used_other_y, (bar_width + bar_overlap),
                         used_other_height)
            cr.fill()

            # Draw loaned from top
            if self.data[iter]["memloaned"] is not None:
                loaned_height = self.data[iter]["memloaned"] / fl_memtotal
                cr.set_source_rgba(self.settings["loaned_rgba"]["r"],
                                   self.settings["loaned_rgba"]["g"],
                                   self.settings["loaned_rgba"]["b"],
                                   self.settings["loaned_rgba"]["a"])
                cr.rectangle(bar_x, 0, (bar_width + bar_overlap), loaned_height)
                cr.fill()

            bar_x -= bar_width

        # Set grouping as source and draw a rounded rectangle mask for the bar
        cr.pop_group_to_source()
        self.draw_rounded_rectangle(cr, 0, 0, 0.5, 1, 0.05)
        cr.fill()

        # Restore graphics context
        cr.restore()

        # Add labels beneath the bar
        self.draw_labels_setup(cr)
        self.draw_label(cr, "Total", self.data[0]["memtotal"])
        if self.data[0]["memloaned"] is not None:
            self.draw_label_and_legend(cr, "Loaned",
                                       self.data[0]["memloaned"],
                                       self.settings["loaned_rgba"])
        self.draw_label_and_legend(cr, "Used", self.data[0]["memused"],
                                   self.settings["used_rgba"])
        self.draw_label_and_legend(cr, "Free", self.data[0]["memfree"],
                                   self.settings["bar_bg"])
        self.draw_label(cr, "Faults", self.data[0]["faults"])
        self.draw_label(cr, "Fault Time",
                        "%.3fs" % (self.data[0]["faulttime"] / 100000.0))

    def redraw_canvas(self):
        if self.window:
            alloc = self.get_allocation()
            self.queue_draw_area(0, 0, alloc.width, alloc.height)
            self.window.process_updates(True)

class iobus_memory_widget(ams_widget):
    """Widget to display system memory metrics based on a gtk.DrawingArea"""
    def __init__(self, data={}):
        super(iobus_memory_widget, self).__init__(data)

        # Graphics colors (used for drawing the objects and the legend)
        self.settings["spare_rgba"] = {"r":0.5, "g":1.0, "b":0.9, "a":0.6}
        self.settings["reserve_used_rgba"] = {"r":0.2, "g":0.6, "b":0.2, "a":0.6}
        self.settings["excess_used_rgba"] = {"r":1.0, "g": 0.4, "b":0.5, "a":0.6}
        self.settings["reserve_rgba"] = {"r":0.2, "g":1.0, "b":0.2, "a":0.8}
        self.settings["high_rgba"] = {"r":1.0, "g":0.2, "b":0.2, "a":0.8}

    def expose(self, widget, event):
        context = widget.window.cairo_create()
        # Set a clip region for the expose event
        context.rectangle(event.area.x, event.area.y, event.area.width,
                            event.area.height)
        context.clip()
        self.draw(context)
        return False

    def draw(self, cr):
        self.draw_setup(cr)
        self.draw_title(cr, "IO Bus Mem")

        # Save graphics context
        cr.save()

        # Set the coordinates and scale
        cr.move_to(0,0)
        cr.translate((self.settings["border"] + self.settings["title_width"]),
                    self.settings["border"])
        cr.scale(((self.settings["bar_width"] * 2) -
                  (self.settings["border"] * 4)),
                 (self.settings["bar_height"] - self.settings["border"]))

        # Create a grouping
        cr.push_group()
        self.draw_bar_bg(cr)

        bar_width = 0.5 / self.hist_size
        bar_x = 0.5 - bar_width
        bar_overlap = 0.005

        # Create a types.FloatType representation of entitled so that
        # division operations used for drawing are not cast to types.IntType
        fl_entitled = float(self.data[0]["entitled"])

        for iter in range(len(self.data)):
            # Draw spare on bottom
            cr.set_source_rgba(self.settings["spare_rgba"]["r"],
                               self.settings["spare_rgba"]["g"],
                               self.settings["spare_rgba"]["b"],
                               self.settings["spare_rgba"]["a"])
            spare_height = (self.data[iter]["spare"] / fl_entitled)
            spare_y = 1 - spare_height
            cr.rectangle(bar_x, spare_y, (bar_width + bar_overlap),
                         spare_height)
            cr.fill()

            # Draw reserve used above spare (reserve value includes spare)
            reserve_used = self.data[iter]["curr"] - (self.data[iter]["spare"] +
                           (self.data[iter]["excess"] -
                            self.data[iter]["excessfree"]))
            reserve_used_height = (reserve_used / fl_entitled)
            reserve_used_y = spare_y - reserve_used_height
            cr.set_source_rgba(self.settings["reserve_used_rgba"]["r"],
                               self.settings["reserve_used_rgba"]["g"],
                               self.settings["reserve_used_rgba"]["b"],
                               self.settings["reserve_used_rgba"]["a"])
            cr.rectangle(bar_x, reserve_used_y, (bar_width + bar_overlap),
                         reserve_used_height)
            cr.fill()

            # Draw excess used on top of reserve usage
            excess_used = (self.data[iter]["excess"] -
                           self.data[iter]["excessfree"])
            excess_used_height = excess_used / fl_entitled
            excess_used_y = reserve_used_y - excess_used_height
            cr.set_source_rgba(self.settings["excess_used_rgba"]["r"],
                               self.settings["excess_used_rgba"]["g"],
                               self.settings["excess_used_rgba"]["b"],
                               self.settings["excess_used_rgba"]["a"])
            cr.rectangle(bar_x, excess_used_y, (bar_width + bar_overlap),
                         excess_used_height)
            cr.fill()

            bar_x -= bar_width

        # Draw dashed line for reserve
        reserve_y = 1 - (self.data[0]["reserve"] / fl_entitled)
        cr.set_source_rgba(self.settings["reserve_rgba"]["r"],
                           self.settings["reserve_rgba"]["g"],
                           self.settings["reserve_rgba"]["b"],
                           self.settings["reserve_rgba"]["a"])
        cr.set_line_width(0.01)
        cr.set_dash([0.025, 0.025], 0.9875)
        cr.move_to(0, reserve_y)
        cr.line_to(0.5, reserve_y)
        cr.stroke()

        # Draw dashed line for high water mark
        high_y = 1 - (self.data[0]["high"] / fl_entitled)
        cr.set_source_rgba(self.settings["high_rgba"]["r"],
                           self.settings["high_rgba"]["g"],
                           self.settings["high_rgba"]["b"],
                           self.settings["high_rgba"]["a"])
        cr.set_line_width(0.01)
        cr.set_dash([0.025, 0.025], 0.0125)
        cr.move_to(0, high_y)
        cr.line_to(0.5, high_y)
        cr.stroke()

        # Set grouping as source and draw a rounded rectangle mask for the bar
        cr.pop_group_to_source()
        self.draw_rounded_rectangle(cr, 0, 0, 0.5, 1, 0.05)
        cr.fill()

        # Restore graphics context
        cr.restore()

        # Add labels beneath the bar
        self.draw_labels_setup(cr)
        self.draw_label(cr, "Entitled", self.data[0]["entitled"])
        self.draw_label(cr, "Desired", self.data[0]["desired"])
        self.draw_label_and_legend(cr, "High water", self.data[0]["high"],
                                   self.settings["high_rgba"])
        self.draw_label(cr, "In use", self.data[0]["curr"])
        self.draw_label_and_legend(cr, "Spare", self.data[0]["spare"],
                                   self.settings["spare_rgba"])
        self.draw_label_and_legend(cr, "Reserve pool", self.data[0]["reserve"],
                                   self.settings["reserve_rgba"])
        self.draw_label_and_legend(cr, "Excess pool", self.data[0]["excess"],
                                   self.settings["excess_used_rgba"])
        self.draw_label(cr, "Excess free", self.data[0]["excessfree"])

    def redraw_canvas(self):
        if self.window:
            alloc = self.get_allocation()
            self.queue_draw_area(0, 0, alloc.width, alloc.height)
            self.window.process_updates(True)

class device_label_widget(ams_widget):
    """Widget to display labels for the devices based on a gtk.DrawingArea"""
    def __init__(self):
        super(device_label_widget, self).__init__()

        # Widget geometry data
        self.settings["data_width"] = 105
        self.settings["bar_min_width"] = 0
        self.settings["bar_min_height"] = 0

        min_width = max((self.settings["title_width"] +
                         self.settings["bar_min_width"]),
                        self.settings["data_width"])
        min_height = max(self.settings["title_height"],
                         (self.settings["bar_min_height"] +
                          self.settings["data_height"]))
        self.set_size_request((min_width + (self.settings["border"] * 2)),
                              (min_height + (self.settings["border"] * 2)))

        # Graphics colors (used for drawing the objects and the legend)
        self.settings["entitled_rgba"] = {"r":0.5, "g":1.0, "b":0.9, "a":0.6}
        self.settings["desired_rgba"] = {"r":1.0, "g": 0.4, "b":0.5, "a":0.6}
        self.settings["allocated_rgba"] = {"r":0.2, "g":0.6, "b":0.2, "a":0.6}

    def expose(self, widget, event):
        context = widget.window.cairo_create()
        # Set a clip region for the expose event
        context.rectangle(event.area.x, event.area.y, event.area.width,
                          event.area.height)
        context.clip()
        self.draw(context)
        return False

    def draw(self, cr):
        self.draw_setup(cr)
        self.draw_title(cr, "IO Devices")

        self.draw_labels_setup(cr)
        self.draw_label(cr, "Device Name", None)
        self.draw_label_and_legend(cr, "Entitled", None,
                                   self.settings["entitled_rgba"])
        self.draw_label_and_legend(cr, "Desired", None,
                                   self.settings["desired_rgba"])
        self.draw_label_and_legend(cr, "Allocated", None,
                                   self.settings["allocated_rgba"])
        self.draw_label(cr, "Allocs failed", None)

    def redraw_canvas(self):
        if self.window:
            alloc = self.get_allocation()
            self.queue_draw_area(0, 0, alloc.width, alloc.height)
            self.window.process_updates(True)

class device_data_widget(ams_widget):
    """Widget to display system memory metrics based on a gtk.DrawingArea"""
    def __init__(self, data={}):
        super(device_data_widget, self).__init__(data)

        # Widget geometry data
        self.settings["title_width"] = 0
        self.settings["data_width"] = 100

        min_width = max((self.settings["title_width"] +
                         self.settings["bar_min_width"]),
                        self.settings["data_width"])
        min_height = max(self.settings["title_height"],
                         (self.settings["bar_min_height"] +
                          self.settings["data_height"]))
        self.set_size_request((min_width + (self.settings["border"] * 2)),
                              (min_height + (self.settings["border"] * 2)))

        # Graphics colors (used for drawing the objects and the legend)
        self.settings["entitled_rgba"] = {"r":0.5, "g":1.0, "b":0.9, "a":0.6}
        self.settings["desired_rgba"] = {"r":1.0, "g": 0.4, "b":0.5, "a":0.6}
        self.settings["allocated_rgba"] = {"r":0.2, "g":0.6, "b":0.2, "a":0.6}
        self.settings["excess_used_rgba"] = {"r":0.4, "g":0.4, "b":0.4, "a":0.9}

    def __cmp__(self, other):
        """Compare device data widgets to other widgets (or anything).

        The comparision is based on the nummerical name of the device, which
        is the bus address for the device.
        """
        if (long(self.data[0]["name"])) < other:
            return -1
        elif (long(self.data[0]["name"])) > other:
            return 1
        else:
            return 0

    def __repr__(self):
        return str(self.data[0]["name"])

    def expose(self, widget, event):
        context = widget.window.cairo_create()
        # Set a clip region for the expose event
        context.rectangle(event.area.x, event.area.y, event.area.width,
                          event.area.height)
        context.clip()
        self.draw(context)
        return False

    def draw(self, cr):
        # Here is an template for how this widget might be used
        self.draw_setup(cr)

        # Save graphics context
        cr.save()

        # Set the coordinates and scale
        cr.move_to(0,0)
        cr.translate((self.settings["border"] + self.settings["title_width"]),
                     self.settings["border"])
        cr.scale(((self.settings["bar_width"] * 2) -
                  (self.settings["border"] * 4)),
                 (self.settings["bar_height"] - self.settings["border"]))

        # Create a grouping
        cr.push_group()
        self.draw_bar_bg(cr)

        bar_width = 0.5 / self.hist_size
        bar_x = 0.5 - bar_width
        bar_overlap = 0.005
        fl_maxavail = float(self.data[0]["maxavail"])
        for iter in range(len(self.data)):
            # Create a types.FloatType representation of maxavail so that
            # division operations used for drawing are not cast to types.IntType

            # Draw allocated on bottom
            cr.set_source_rgba(self.settings["allocated_rgba"]["r"],
                               self.settings["allocated_rgba"]["g"],
                               self.settings["allocated_rgba"]["b"],
                               self.settings["allocated_rgba"]["a"])
            spare_height = (self.data[iter]["allocated"] / fl_maxavail)
            spare_y = 1 - spare_height
            cr.rectangle(bar_x, spare_y, (bar_width + bar_overlap),
                         spare_height)
            cr.fill()

            # Draw excess used from top
            excess = self.data[iter]["allocated"] - self.data[iter]["entitled"]
            if excess <= 0:
                excess = 0
            excess_used = self.data[iter]["excess_used"] - excess
            excess_used_height = excess_used / fl_maxavail
            cr.set_source_rgba(self.settings["excess_used_rgba"]["r"],
                               self.settings["excess_used_rgba"]["g"],
                               self.settings["excess_used_rgba"]["b"],
                               self.settings["excess_used_rgba"]["a"])
            cr.rectangle(bar_x, 0, (bar_width + bar_overlap),
                         excess_used_height)
            cr.fill()

            bar_x -= bar_width

        # Draw dashed line for entitled
        entitled_y = 1 - (self.data[0]["entitled"] / fl_maxavail)
        cr.set_source_rgb(self.settings["entitled_rgba"]["r"],
                           self.settings["entitled_rgba"]["g"],
                           self.settings["entitled_rgba"]["b"])
        cr.set_line_width(0.01)
        cr.set_dash([0.025, 0.0125], 0)
        cr.move_to(0, entitled_y)
        cr.line_to(0.5, entitled_y)
        cr.stroke()

        # Set grouping as source and draw a rounded rectangle mask for the bar
        cr.pop_group_to_source()
        self.draw_rounded_rectangle(cr, 0, 0, 0.5, 1, 0.05)
        cr.fill()

        # Restore graphics context
        cr.restore()

        # Add labels beneath the bar
        self.draw_labels_setup(cr)
        self.draw_label(cr, None, self.data[0]["name"])
        self.draw_label(cr, None, self.data[0]["entitled"])
        self.draw_label(cr, None, self.data[0]["desired"])
        self.draw_label(cr, None, self.data[0]["allocated"])
        self.draw_label(cr, None, self.data[0]["allocs_failed"])

    def redraw_canvas(self):
        if self.window:
            alloc = self.get_allocation()
            self.queue_draw_area(0, 0, alloc.width, alloc.height)
            self.window.process_updates(True)
