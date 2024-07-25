# RBXGS Console Host
 
This is a console host for [RBXGS](https://twitter.com/boxerpizza/status/1675670773564862465), allowing you to run it without needing IIS, effectively making it work a lot like RCCService. At its core it's basically just an incredibly barebones [ISAPI](https://learn.microsoft.com/en-us/previous-versions/iis/6.0-sdk/ms525172(v=vs.90)) extension host emulator.

To use it, copy the RBXGS DLLs to the same folder as RBXGSConHost.exe, and run it.

This *must* be compiled using MSVC 8 on Visual Studio 2005. Releases are given if you don't feel like spending a few hours setting up an environment to do that, or dealing with the 40% chance of it crashing whenever you click on it.

The content folder must be placed at `C:\ProgramData\Roblox\Content`.

Things that still need doing:
- Make Ctrl+C work
- Path selection for content folder

![image-1](https://github.com/user-attachments/assets/96f46aec-be4c-4470-97b0-9f2e809722ba)
