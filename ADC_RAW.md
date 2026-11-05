# Live ADC RAW readings

Service: `91bad4b0-b950-4226-aa2b-4ede9fa42f59`.
Characteristic: `91bad4b1-b950-4226-aa2b-4ede9fa42f59` (READ / NOTIFY; not writable).

The fixed 8-byte version 1 packet uses little-endian integers:

| Bytes | Meaning |
| --- | --- |
| 0 | Version, always 1 |
| 1 | Validity mask: bit 0 battery, bit 1 current 1, bit 2 current 2 |
| 2–3 | ADC_BAT / PA5 / ADC4 channel 4, uint16 RAW |
| 4–5 | ADC1 / PA3 / ADC4 channel 6, MPQ #1 FT/CS, uint16 RAW |
| 6–7 | ADC2 / PB9 / ADC4 channel 10, MPQ #2 FT/CS, uint16 RAW |

Valid readings are 12-bit counts, 0–4095. A cleared validity bit means the field
must not be displayed as a measurement. No conversion to voltage or current is
performed. Future calibrated units require a new packet version or characteristic.

TIM2 triggers an ADC scan at 1 kHz. The scan has six ranks: BAT/BAT, ADC1/ADC1,
ADC2/ADC2, retaining the second conversion of each pair to allow mux settling.
HCLK / 8 and the 814.5-cycle sampling time are retained. GPDMA1 channel 6 runs a
circular 64-sequence buffer. Half/full callbacks accumulate 32 samples per input;
no BLE work or divisions occur in the ISR. ADC_READ returns the latest individual
sample without changing the sequencer. Sampling continues while disconnected.

Every approximately 200 ms, defaultTask drains the accumulators and computes
`round(sum(x*x) / sum(x))` independently for each input (zero if all samples are
zero). This is an amplitude-weighted mean: 99 readings of 100 plus one of 2000
produce 419, compared with 119 for the arithmetic mean. It emphasizes short high
pulses; it is not a peak hold or a measurement in engineering units.

Only subscribed connections receive the filtered windows. READ returns the latest
published snapshot. Before the first window and after notifications stop, validity
is zero. Stale DMA data or ADC/DMA errors clear validity; BLE congestion drops the
frame. Accumulators are drained even without subscribers, avoiding old data at
reconnection. Motor control and BLE dispatch have higher task priority than this
publisher. DMA/ADC IRQ priority 7 is below I2C's interrupt priority.

Web shows the three RAW numbers in the sidebar's ADC RAW card, clears them on
disconnect or a stale stream, and handles older firmware without this service.

Hardware verification: `python tools/check_adc_ble.py --address 00:80:E1:2A:26:24`.
Close other BLE clients first. This test does not issue servo movement commands.
`tools/test_adc_filter.c` tests zero, steady input, a short high pulse, full scale,
and accumulator bounds using the actual filter implementation on a host compiler.
