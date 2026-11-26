# MapGyver 2

MapGyver is a desktop application for video mapping and live media composition. The product lives in [apps/mapgyver](apps/mapgyver/README.md). The runtime, desktop hosts, and shared UI live in [Golden Core](https://github.com/Golden-Geek/GoldenCore), pinned at `golden/`.

```sh
git clone --recurse-submodules https://github.com/Golden-Geek/MapGyver2.git
cd MapGyver2
npm ci
npm run check
npm run build
```

For an existing checkout, run `git submodule update --init --recursive`. Use the pinned Rust and JavaScript versions in [toolchain setup](tools/bootstrap/README.md), and the platform SDK instructions in [the FFmpeg guide](tools/mapgyver/ffmpeg/README.md). On Windows, `npm run run:mapgyver` prepares the SDK before launching the app. The desktop and headless commands are listed in the product README.

Continue product work from the [implementation plan](docs/plan/MapGyver_Implementation_Plan.md) and [status](docs/progress/mapgyver-status.md). Historical MapGyver 2 prereleases through `1.0.0b4` are linked from the [original Chataigne2 release page](https://github.com/Golden-Geek/Chataigne2/releases).
