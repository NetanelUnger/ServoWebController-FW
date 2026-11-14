"""Windows-only hardware test helper: briefly disable/restore Bluetooth, not Wi-Fi.

This interrupts ALL local Bluetooth connections. Use only with explicit --run
authorization. The original radio state is restored in finally. It tests real
link loss when Windows retains a connection after a GATT client is disposed.
"""
import argparse
import asyncio


async def cycle_bluetooth(seconds=6):
    from winrt.windows.devices.radios import Radio, RadioKind, RadioState, RadioAccessStatus
    radios = [r for r in await Radio.get_radios_async()
              if r.kind == RadioKind.BLUETOOTH and r.state == RadioState.ON]
    if len(radios) != 1:
        raise RuntimeError('Expected exactly one enabled Bluetooth radio')
    radio = radios[0]
    original = radio.state
    try:
        result = await radio.set_state_async(RadioState.OFF)
        if result != RadioAccessStatus.ALLOWED:
            raise RuntimeError(('Bluetooth OFF denied', result))
        print(f'Bluetooth OFF for {seconds}s (intentional link-loss test)', flush=True)
        await asyncio.sleep(seconds)
    finally:
        result = await radio.set_state_async(original)
        if result != RadioAccessStatus.ALLOWED:
            raise RuntimeError(('Bluetooth state restoration failed', result))
        print('Bluetooth restored ON', flush=True)
    await asyncio.sleep(2)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--run', action='store_true')
    args = parser.parse_args()
    if not args.run:
        parser.error('Explicit --run is required; all Bluetooth connections are interrupted')
    asyncio.run(cycle_bluetooth())
