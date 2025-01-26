# RBXGSConHost
 
A runtime environment for RBXGS, allowing it to be ran standalone without IIS (effectively making it work like RCCService).

![explorer_CFJJDrBNcV](https://github.com/user-attachments/assets/d15a99a4-1daf-4522-928d-05c38bc698d2)

# Usage

```
  -h, --help            Print this help message
  -p, --port <port>     Specify web server port (default is 64989)
  -b, --baseDir <path>  Specify path where RBXGS is located (default is working dir)
```

You'll need to extract the files from the setup MSI using something like [lessmsi](https://github.com/activescott/lessmsi) to ensure you have all the files needed.

The location of the content folder is assumed to be in the base directory, like how a typical Roblox install is laid out. The MSI stores it in the CommonAppData folder, which you'll have to move it out of.

StandardOut redirection will not work on XP/Server 2003 and older versions of Windows.

# Code

Must be built with MSVC 8.0 (Visual Studio 2005), otherwise it kind of just won't work properly.

Uses [MinHook](https://github.com/TsudaKageyu/minhook).
