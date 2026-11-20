#include "hwdecodeprobe.h"

#include <QLibrary>
#include <QtGlobal>

#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <vector>

namespace HwDecodeProbe {

namespace {

// The few libva definitions used here (va/va.h), so that building does not
// require the libva headers. These values are part of the stable libva 2 ABI.
using VADisplay = void *;
using VAStatus = int;
using VAMessageCallback = void (*)(void *context, const char *message);
constexpr VAStatus VaStatusSuccess = 0;
constexpr int VaProfileAv1Profile0 = 32;
constexpr int VaEntrypointVld = 1;

constexpr char AutoMarker[] = "LURVIKO_HW_DECODE_AUTO";
constexpr char DeviceTypesVariable[] = "QT_FFMPEG_DECODING_HW_DEVICE_TYPES";
constexpr char DefaultDeviceTypes[] = "vaapi,cuda";
constexpr char SoftwareOnly[] = ",";

void silentMessage(void *, const char *) {}

} // namespace

Av1Support probeVaapiAv1()
{
    QLibrary va(QStringLiteral("va"), 2);
    QLibrary vaDrm(QStringLiteral("va-drm"), 2);
    if (!va.load() || !vaDrm.load())
        return Av1Support::Unknown;

    using GetDisplayDrm = VADisplay (*)(int);
    using Initialize = VAStatus (*)(VADisplay, int *, int *);
    using Terminate = VAStatus (*)(VADisplay);
    using MaxCount = int (*)(VADisplay);
    using QueryProfiles = VAStatus (*)(VADisplay, int *, int *);
    using QueryEntrypoints = VAStatus (*)(VADisplay, int, int *, int *);
    using SetCallback = VAMessageCallback (*)(VADisplay, VAMessageCallback, void *);

    const auto getDisplay = reinterpret_cast<GetDisplayDrm>(vaDrm.resolve("vaGetDisplayDRM"));
    const auto initialize = reinterpret_cast<Initialize>(va.resolve("vaInitialize"));
    const auto terminate = reinterpret_cast<Terminate>(va.resolve("vaTerminate"));
    const auto maxProfiles = reinterpret_cast<MaxCount>(va.resolve("vaMaxNumProfiles"));
    const auto queryProfiles = reinterpret_cast<QueryProfiles>(va.resolve("vaQueryConfigProfiles"));
    const auto maxEntrypoints = reinterpret_cast<MaxCount>(va.resolve("vaMaxNumEntrypoints"));
    const auto queryEntrypoints = reinterpret_cast<QueryEntrypoints>(va.resolve("vaQueryConfigEntrypoints"));
    const auto setInfoCallback = reinterpret_cast<SetCallback>(va.resolve("vaSetInfoCallback"));
    if (!getDisplay || !initialize || !terminate || !maxProfiles || !queryProfiles
            || !maxEntrypoints || !queryEntrypoints)
        return Av1Support::Unknown;

    // FFmpeg's VA-API device without an explicit path is the first render
    // node that opens; Qt then uses it whenever vaInitialize succeeds.
    int fd = -1;
    for (int n = 0; n < 8 && fd < 0; ++n) {
        const QByteArray path = "/dev/dri/renderD" + QByteArray::number(128 + n);
        fd = ::open(path.constData(), O_RDWR | O_CLOEXEC);
    }
    if (fd < 0)
        return Av1Support::Unknown;

    Av1Support result = Av1Support::Unknown;
    if (VADisplay display = getDisplay(fd)) {
        if (setInfoCallback)
            setInfoCallback(display, silentMessage, nullptr);
        int major = 0;
        int minor = 0;
        if (initialize(display, &major, &minor) == VaStatusSuccess) {
            result = Av1Support::Unsupported;
            std::vector<int> profiles(std::max(0, maxProfiles(display)));
            int profileCount = 0;
            if (!profiles.empty()
                    && queryProfiles(display, profiles.data(), &profileCount) == VaStatusSuccess) {
                profiles.resize(std::clamp(profileCount, 0, int(profiles.size())));
                if (std::find(profiles.begin(), profiles.end(), VaProfileAv1Profile0) != profiles.end()) {
                    std::vector<int> entrypoints(std::max(0, maxEntrypoints(display)));
                    int entrypointCount = 0;
                    if (!entrypoints.empty()
                            && queryEntrypoints(display, VaProfileAv1Profile0, entrypoints.data(),
                                                &entrypointCount) == VaStatusSuccess) {
                        entrypoints.resize(std::clamp(entrypointCount, 0, int(entrypoints.size())));
                        if (std::find(entrypoints.begin(), entrypoints.end(), VaEntrypointVld)
                                != entrypoints.end())
                            result = Av1Support::Supported;
                    }
                }
            }
            terminate(display);
        }
    }
    ::close(fd);
    return result;
}

QByteArray decodingDeviceTypes(Av1Support av1)
{
    // Unknown keeps hardware decoding: VA-API is then unusable anyway and
    // CUDA cannot be queried without the NVIDIA SDK headers.
    return av1 == Av1Support::Unsupported ? QByteArray(SoftwareOnly) : QByteArray(DefaultDeviceTypes);
}

void configureEnvironment()
{
    const bool inherited = !qEnvironmentVariableIsEmpty(AutoMarker);
    if (!inherited && !qEnvironmentVariableIsEmpty(DeviceTypesVariable))
        return;
    qputenv(DeviceTypesVariable, decodingDeviceTypes(probeVaapiAv1()));
    qputenv(AutoMarker, QByteArrayLiteral("1"));
}

} // namespace HwDecodeProbe
