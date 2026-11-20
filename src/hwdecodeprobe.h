#pragma once

#include <QByteArray>

// Chooses the hardware decoders Qt Multimedia's FFmpeg backend may use.
//
// Qt reads QT_FFMPEG_DECODING_HW_DEVICE_TYPES once per process and, for a
// listed device type, picks any FFmpeg decoder that can output its surfaces
// without checking that the GPU actually decodes the codec. For AV1 that is
// FFmpeg's native "av1" decoder, which works only through a hardware
// accelerator: on GPUs without AV1 decoding (e.g. Intel before Tiger Lake,
// NVIDIA before Ampere) every frame fails and the player shows no picture
// while the audio plays. The software libdav1d decoder is chosen only when
// hardware decoding is off, so it is turned off on such systems.
namespace HwDecodeProbe {

enum class Av1Support { Supported, Unsupported, Unknown };

// Asks the VA-API driver of the render node FFmpeg opens by default
// (the first openable /dev/dri/renderD*) whether it decodes AV1 profile 0.
// libva is loaded at run time; Unknown when it or the device is unavailable.
Av1Support probeVaapiAv1();

// The value for QT_FFMPEG_DECODING_HW_DEVICE_TYPES ("," disables hardware
// decoding) for the given probe result.
QByteArray decodingDeviceTypes(Av1Support av1);

// Sets QT_FFMPEG_DECODING_HW_DEVICE_TYPES unless the user chose a value.
// A value inherited from another Lurviko process (marked by
// LURVIKO_HW_DECODE_AUTO) is re-evaluated rather than treated as a choice.
void configureEnvironment();

} // namespace HwDecodeProbe
