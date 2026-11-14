# Performance benchmarks

The benchmark suite should record repeatable results rather than inventing performance claims.

## Required measurements

- Cold boot to usable home screen.
- App launch and close latency.
- UI frame time / missed frame count.
- Free heap and largest free block at idle and under load.
- microSD sequential read/write throughput and error rate.
- App package scan/install time.
- Battery voltage and power draw when the required measurement hardware is available.

## Result format

Store one JSON file per run with:
- firmware commit and build configuration
- board model and hardware revision
- microSD make/model/filesystem
- ambient conditions when relevant
- raw measurements, units and test duration

Never compare results from different hardware or test conditions without labeling the differences.
