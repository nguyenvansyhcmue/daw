# Enabling ASIO on Windows

StudioForge enables ASIO only when the licensed Steinberg ASIO SDK is present
at build time. This is required by JUCE and Steinberg's SDK licence.

1. Install the official driver for the audio interface. StudioForge currently
   detects `Realtek ASIO` on this computer.
2. Download the Steinberg ASIO SDK from Steinberg and accept its licence.
3. Configure CMake with the SDK root. The root must contain `common/iasiodrv.h`.

```powershell
cmake -S . -B build-ui -DSTUDIOFORGE_ASIO_SDK_DIR="C:/path/to/asiosdk"
cmake --build build-ui --config Debug
```

After rebuilding, `ASIO` appears in **Configure audio device...** alongside
Windows Audio. Select the hardware ASIO driver, then set sample rate and buffer
size there. Use 64 or 128 samples for low-latency live monitoring when the
hardware can run reliably at that setting.
