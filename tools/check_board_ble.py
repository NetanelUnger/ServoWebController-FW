"""Board bring-up checks over NUS. Never enables power or PWM outputs."""
import argparse
import asyncio
import json
import re
from datetime import datetime, timezone
from pathlib import Path

from bleak import BleakClient, BleakScanner

RX = '6e400002-b5a3-f393-e0a9-e50e24dcca9e'
TX = '6e400003-b5a3-f393-e0a9-e50e24dcca9e'
OUTPUTS = [('EN1', 'PB5', 0), ('EN2', 'PB4', 0), ('DIAG_EN1', 'PA10', 0),
           ('DIAG_EN2', 'PA11', 0), ('OE', 'PA9', 1), ('DISPLAY_RES', 'PA2', 1),
           ('DISPLAY_CS', 'PA4', 1), ('DISPLAY_DC', 'PB11', 0)]


async def check(args):
    found = await BleakScanner.find_device_by_address(args.address, timeout=15)
    assert found is not None, 'Target did not advertise'
    outputs = [(name, pin, 0 if args.display_disabled and name == 'DISPLAY_RES' else level)
               for name, pin, level in OUTPUTS]
    report = {'utc': datetime.now(timezone.utc).isoformat(), 'address': found.address,
              'name': found.name, 'checks': []}
    async with BleakClient(found, winrt={'use_cached_services': False}, timeout=20) as client:
        replies = asyncio.Queue()
        await client.start_notify(TX, lambda _, data: replies.put_nowait(bytes(data).decode()))

        async def command(request, expected=None, pattern=None):
            data = request.encode() if isinstance(request, str) else request
            await client.write_gatt_char(RX, data, response=True)
            response = await asyncio.wait_for(replies.get(), timeout=5)
            if expected is not None:
                assert response == expected, (request, response, expected)
            if pattern is not None:
                assert re.fullmatch(pattern, response), (request, response, pattern)
            report['checks'].append({'request': data.decode(), 'response': response})
            print(f'{data!r} -> {response}', flush=True)
            return response

        async def help_command():
            expected = ['OK HELP', 'HELP', 'PING', 'GPIO_GET <pin>', 'GPIO_SET <pin> <0|1>',
                        'ADC_READ <pin>', 'SET_DIRECT <ch> <us>', 'SET_TIMING', ' <ch> <us> <ms>',
                        'TOF_SET_MIN_MAX', ' <min_mm> <max_mm>', 'PCA9685_SET_SERVO_US',
                        ' = SET_DIRECT', 'servo_StartMove', ' = SET_TIMING', 'OK HELP END']
            await client.write_gatt_char(RX, b'HELP', response=True)
            received = []
            for line in expected:
                response = await asyncio.wait_for(replies.get(), timeout=5)
                assert response == line, (response, line)
                assert len(response.encode()) <= 20
                received.append(response)
            report['checks'].append({'request': 'HELP', 'response': '\n'.join(received)})
            print('HELP -> all 16 lines received in order, including completion marker', flush=True)

        await help_command()
        await command('HELP extra', '- BAD ARGUMENTS')
        await command('help', '- NOT SUPPORTED')
        await command('PING', 'OK PING')
        for name, pin, level in outputs:
            for target in [name, pin]:
                await command(f'GPIO_GET {target}', f'OK GPIO_GET {name} {level}')
        for target in ['INT_TOF', 'PC13']:
            await command(f'GPIO_GET {target}', pattern=r'OK GPIO_GET INT_TOF [01] IRQ \d+')
        for name, pin in [('ADC1', 'PA3'), ('ADC2', 'PB9'), ('ADC_BAT', 'PA5')]:
            for target in [name, pin]:
                response = await command(f'ADC_READ {target}', pattern=rf'OK ADC_READ {name} \d+')
                assert 0 <= int(response.split()[-1]) <= 4095

        for request, error in [
            ('GPIO_SET INT_TOF 1', '- READ ONLY'), ('GPIO_SET PC13 0', '- READ ONLY'),
            ('GPIO_SET PA13 1', '- UNKNOWN PIN'), ('GPIO_SET PA7 0', '- UNKNOWN PIN'),
            ('GPIO_SET PB8 1', '- UNKNOWN PIN'), ('GPIO_SET ADC1 1', '- UNKNOWN PIN'),
            ('ADC_READ EN1', '- UNKNOWN PIN'), ('ADC_READ PA4', '- UNKNOWN PIN'),
            ('GPIO_GET INVALID', '- UNKNOWN PIN'), ('GPIO_GET PA3', '- UNKNOWN PIN'),
            ('GPIO_SET EN1 2', '- BAD ARGUMENTS'), ('GPIO_SET EN1 -1', '- BAD ARGUMENTS'),
            ('GPIO_SET EN1 1junk', '- BAD ARGUMENTS'), ('GPIO_SET EN1 1 0', '- BAD ARGUMENTS'),
            ('GPIO_SET EN1', '- BAD ARGUMENTS'), ('GPIO_GET', '- BAD ARGUMENTS'),
            ('ADC_READ', '- BAD ARGUMENTS'), ('ADC_READ PA3 extra', '- BAD ARGUMENTS'),
            (b'GPIO_SET EN1 1\x00junk', '- BAD ARGUMENTS'),
            ('SET_DIRECT 0 1500', '- PCA9685 DISABLED'),
            ('TOF_SET_MIN_MAX 0 1000', '- VL53L5CX DISABLED'),
        ]:
            await command(request, error)
        # Benign writes exercise the command without energizing power/PWM paths.
        for name, pin, level in outputs:
            await command(f'GPIO_SET {pin} {level}', f'OK GPIO_SET {name} {level}')
        if args.exercise_display_dc:
            try:
                await command('GPIO_SET DISPLAY_DC 1', 'OK GPIO_SET DISPLAY_DC 1')
                await command('GPIO_GET PB11', 'OK GPIO_GET DISPLAY_DC 1')
            finally:
                await command('GPIO_SET DISPLAY_DC 0', 'OK GPIO_SET DISPLAY_DC 0')
        for name, _, level in outputs:
            await command(f'GPIO_GET {name}', f'OK GPIO_GET {name} {level}')
        await command('PING', 'OK PING')
        await help_command()
    found = await BleakScanner.find_device_by_address(args.address, timeout=15)
    assert found is not None, 'Target did not resume advertising after disconnect'
    report['advertises_after_disconnect'] = True
    report['pass'] = True
    if args.report:
        Path(args.report).write_text(json.dumps(report, indent=2) + '\n')
    print(f"PASS: {len(report['checks'])} NUS checks; advertising after disconnect")


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--address', required=True, help='Explicit BLE address of the target board')
    parser.add_argument('--exercise-display-dc', action='store_true')
    parser.add_argument('--display-disabled', action='store_true', help='Expect DISPLAY_RES low when ENABLE_DISPLAY=0')
    parser.add_argument('--report', help='JSON result file')
    asyncio.run(check(parser.parse_args()))
