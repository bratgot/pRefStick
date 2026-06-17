import nuke, os
mm = "%d.%d" % (nuke.env['NukeVersionMajor'], nuke.env['NukeVersionMinor'])
p = os.path.expanduser("~/.nuke/plugins/" + mm)
if os.path.isdir(p):
    nuke.pluginAddPath(p)