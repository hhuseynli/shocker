# Shocker
Shocker is a utility for managing quick, lightweight, and persistent development environments. <br>
Shocker requires root privileges to work, since it uses user namespaces and OverlayFS for isolating environments. <br>
``` make && sudo ./shocker ``` to run
<br>
Note: If you get an error like "The directory base/ doesn't exist" or "base/ exists but isn't a directory", ensure that you have the .shocker directory by executing the following command: <br>
``` mkdir .shocker ```
