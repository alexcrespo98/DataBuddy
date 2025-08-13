import sys

try:
    from bluetooth import discover_devices, BluetoothError
except ImportError:
    print("pybluez not installed. Please run: pip install pybluez")
    sys.exit(1)

def scan_bluetooth_devices():
    print("Scanning for Bluetooth devices...")
    try:
        devices = discover_devices(duration=8, lookup_names=True)
        if not devices:
            print("No Bluetooth devices found. Make sure Bluetooth is enabled and you have permissions.")
        else:
            print(f"Found {len(devices)} devices:")
            for addr, name in devices:
                print(f"  {name} [{addr}]")
    except BluetoothError as e:
        print(f"Bluetooth error: {e}")
        print("Try running with elevated permissions (e.g., 'Run as Administrator').")
    except Exception as e:
        print(f"Unexpected error: {e}")

def main():
    scan_bluetooth_devices()

if __name__ == "__main__":
    main()
