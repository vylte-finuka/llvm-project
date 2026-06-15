#!/usr/bin/env python3
"""
Vyft Ltd - Maratine Language File Type Registration
Registers *.mart extension with the system
"""

import os
import sys
import platform

def register_maratine_extension():
    """Register .mart extension as Maratine language"""
    
    system = platform.system()
    
    if system == "Windows":
        register_windows()
    elif system == "Darwin":
        register_macos()
    elif system == "Linux":
        register_linux()
    else:
        print(f"Unsupported system: {system}")
        return False
    
    return True

def register_windows():
    """Register .mart for Windows"""
    try:
        import winreg
        
        # HKEY_CLASSES_ROOT\.mart
        registry_path = r"Software\Classes\.mart"
        
        with winreg.CreateKey(winreg.HKEY_CURRENT_USER, registry_path) as key:
            winreg.SetValueEx(key, None, 0, winreg.REG_SZ, "MaratineFile")
            winreg.SetValueEx(key, "Content Type", 0, winreg.REG_SZ, "application/x-maratine")
        
        # Register Maratine file type
        app_path = r"Software\Classes\MaratineFile"
        with winreg.CreateKey(winreg.HKEY_CURRENT_USER, app_path) as key:
            winreg.SetValueEx(key, None, 0, winreg.REG_SZ, "Maratine Source File")
        
        print("✓ Windows: .mart extension registered")
        
    except Exception as e:
        print(f"✗ Windows registration failed: {e}")
        return False

def register_macos():
    """Register .mart for macOS"""
    try:
        import plistlib
        
        plist_path = os.path.expanduser(
            "~/Library/LaunchAgents/com.vyft.maratine.plist"
        )
        
        plist_data = {
            "CFBundleTypeName": "Maratine Source Code",
            "CFBundleTypeExtensions": ["mart"],
            "CFBundleTypeIconFile": "maratine.icns",
            "CFBundleTypeRole": "Editor",
            "CFBundleTypeMIMETypes": "application/x-maratine"
        }
        
        with open(plist_path, 'wb') as f:
            plistlib.dump(plist_data, f)
        
        print("✓ macOS: .mart extension registered")
        
    except Exception as e:
        print(f"✗ macOS registration failed: {e}")
        return False

def register_linux():
    """Register .mart for Linux"""
    try:
        mime_file = os.path.expanduser(
            "~/.local/share/mime/packages/maratine.xml"
        )
        
        os.makedirs(os.path.dirname(mime_file), exist_ok=True)
        
        mime_content = '''<?xml version="1.0" encoding="UTF-8"?>
<mime-info xmlns="http://www.freedesktop.org/standards/shared-mime-info">
  <mime-type type="application/x-maratine">
    <comment>Maratine source code</comment>
    <glob pattern="*.mart"/>
    <generic-icon name="text-x-generic"/>
  </mime-type>
</mime-info>
'''
        
        with open(mime_file, 'w') as f:
            f.write(mime_content)
        
        # Update MIME database
        os.system("update-mime-database ~/.local/share/mime/")
        
        print("✓ Linux: .mart extension registered")
        
    except Exception as e:
        print(f"✗ Linux registration failed: {e}")
        return False

if __name__ == "__main__":
    print("=== Maratine (.mart) File Type Registration ===\n")
    
    success = register_maratine_extension()
    
    if success:
        print("\n✓ Registration complete!")
        print("  .mart files are now associated with Maratine language")
    else:
        print("\n✗ Registration failed")
        sys.exit(1)
