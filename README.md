# Shocker
Shocker is a utility for managing quick, lightweight, and persistent development environments. <br>
Shocker requires root privileges to work, since it uses user namespaces and OverlayFS for isolating environments. <br>
``` make && sudo ./shocker ``` to run
# IMPORTANT
Currently, Shocker has only been tested on Fedora 44, with the DNF5 package manager. It is highly recommended you use Fedora to run Shocker.
<br>
<br>
Note: If you get an error like "The directory base/ doesn't exist" or "base/ exists but isn't a directory", ensure that you have the .shocker directory by executing the following command: <br>
``` mkdir .shocker ```
