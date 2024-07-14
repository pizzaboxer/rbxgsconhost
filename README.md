# RBXGS Console Host
 
This is a console host emulator for [RBXGS](https://twitter.com/boxerpizza/status/1675670773564862465), allowing you to run it without needing IIS. At its core it's basically just an incredibly barebones [ISAPI](https://learn.microsoft.com/en-us/previous-versions/iis/6.0-sdk/ms525172(v=vs.90)) host emulator.

To use it, copy the RBXGS DLLs to the same folder as LoaderTest.exe, and run it.

Excuse the horrid code, this is my first time touching C++ in like over 2 years or something. Don't run this in a production environment (yet) please. There's a lot of things to currently be fixed and improved. As it stands this is just a rough proof-of-concept.

This *must* be compiled using MSVC 8 on Visual Studio 2005.

Things that still need doing:
- Make Ctrl+C work
- Allowing the port to be set
- Replacing the HTTP parser
- Fixing any lazily-done memory operations that are vulnerable (relates to above point)
- StandardOut console printing hook with symbols