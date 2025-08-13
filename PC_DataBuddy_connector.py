import asyncio
from bleak import BleakScanner

async def scan_ble_devices():
    print("Scanning for BLE (Bluetooth Low Energy) devices...")
    devices = await BleakScanner.discover(timeout=8)
    if not devices:
        print("No BLE devices found. Make sure Bluetooth is enabled and your ESP32C is broadcasting.")
    else:
        print(f"Found {len(devices)} device(s):")
        for d in devices:
            print(f"  {d.name or 'Unknown'} [{d.address}]")

def main():
    asyncio.run(scan_ble_devices())

if __name__ == "__main__":
    main()
