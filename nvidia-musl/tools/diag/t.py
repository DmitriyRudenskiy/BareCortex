import os
d = open(os.path.expanduser("~/lite/glwork/crash/m005_00007de1b5199000_00007de1b600a000_3399000.bin"), "rb").read()
t = b"St12system_error\0"
print("len", len(d), "find:", d.find(t))
