# RBXGS Console Host
 
A console host for RBXGS, allowing you to run it without needing IIS, effectively making it work a lot like RCCService. At its core it's basically just an incredibly barebones [ISAPI extension](https://learn.microsoft.com/en-us/previous-versions/iis/6.0-sdk/ms525172(v=vs.90)) host emulator.

The base directory is the directory where the RBXGS binaries and content folder are located. By default, it is the folder that RBXGSConHost.exe is in.

Usage:
- `-b, --baseDir "<absolute path>"` - Set base directory, absolute path, wrapped in double quotes, no trailing backslash
- `-p, --port <port>` - Set Web Service port, default is 64989

This *must* be compiled using MSVC 8 on Visual Studio 2005. Releases are given if you don't feel like spending a few hours setting up an environment to do that, or dealing with the 40% chance of it crashing whenever you click on it.

![explorer_GZ630N2iI9](https://github.com/user-attachments/assets/620d569c-403d-405d-98ae-60372e4070ca)
