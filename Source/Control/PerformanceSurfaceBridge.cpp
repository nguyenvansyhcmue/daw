#include "PerformanceSurfaceBridge.h"

#include <memory>

#if JUCE_MAC || JUCE_LINUX
 #include <cerrno>
 #include <cstring>
 #include <sys/select.h>
 #include <sys/socket.h>
 #include <sys/un.h>
 #include <sys/stat.h>
 #include <unistd.h>
#endif

namespace
{
constexpr auto configurationFileName = "studioforge-performance-surface.json";

juce::File bridgeDirectory()
{
    return juce::File::getSpecialLocation(juce::File::tempDirectory);
}

juce::String errorPayload(const juce::String& message)
{
    auto root = juce::var(new juce::DynamicObject());
    root.getDynamicObject()->setProperty("ok", false);
    root.getDynamicObject()->setProperty("error", message);
    return juce::JSON::toString(root, false);
}
}

PerformanceSurfaceBridge::PerformanceSurfaceBridge(TrackDataModel& model, PerformanceMacroService& macroService)
    : juce::Thread("StudioForge Performance Surface"), dataModel(model), macros(macroService),
      sessionToken(juce::Uuid().toString().replaceCharacters("-", ""))
{
}

PerformanceSurfaceBridge::~PerformanceSurfaceBridge()
{
    stop();
}

juce::File PerformanceSurfaceBridge::getConfigurationFile() const
{
    return bridgeDirectory().getChildFile(configurationFileName);
}

juce::Result PerformanceSurfaceBridge::start()
{
    if (accepting.load(std::memory_order_acquire))
        return juce::Result::ok();

   #if JUCE_MAC || JUCE_LINUX
    // macOS gives applications a long per-user temporary directory under
    // /var/folders.  Adding the random session token to that path can exceed
    // sockaddr_un::sun_path (104 bytes), which makes bind() fail silently from
    // the companion UI's perspective.  Keep the socket itself in /tmp where
    // its path is short; the random token plus owner-only permissions keep it
    // session-private.  The configuration file remains in the per-user temp
    // directory that the companion already reads.
    const auto socketDirectory = juce::File("/tmp");
    socketPath = socketDirectory.getChildFile("studioforge-performance-" + sessionToken + ".socket")
                     .getFullPathName();
    if (socketPath.getNumBytesAsUTF8() >= static_cast<int>(sizeof(sockaddr_un::sun_path)))
        return juce::Result::fail("Performance surface socket path is too long");

    ::unlink(socketPath.toRawUTF8());
    listeningSocket = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (listeningSocket < 0)
        return juce::Result::fail("Cannot create local performance-surface socket");

    sockaddr_un address {};
    address.sun_family = AF_UNIX;
    std::strncpy(address.sun_path, socketPath.toRawUTF8(), sizeof(address.sun_path) - 1);
    if (::bind(listeningSocket, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0
        || ::listen(listeningSocket, 4) != 0)
    {
        const auto error = juce::String(std::strerror(errno));
        ::close(listeningSocket);
        listeningSocket = -1;
        ::unlink(socketPath.toRawUTF8());
        return juce::Result::fail("Cannot listen for Tune-AI: " + error);
    }
    ::chmod(socketPath.toRawUTF8(), S_IRUSR | S_IWUSR);
    accepting.store(true, std::memory_order_release);
    if (const auto result = writeConfiguration(); result.failed())
    {
        stop();
        return result;
    }
    startThread(juce::Thread::Priority::normal);
    return juce::Result::ok();
   #else
    return juce::Result::fail("Performance surface transport requires a platform local IPC implementation");
   #endif
}

void PerformanceSurfaceBridge::stop()
{
    accepting.store(false, std::memory_order_release);
   #if JUCE_MAC || JUCE_LINUX
    if (listeningSocket >= 0)
    {
        ::shutdown(listeningSocket, SHUT_RDWR);
        ::close(listeningSocket);
        listeningSocket = -1;
    }
   #endif
    signalThreadShouldExit();
    stopThread(1000);
   #if JUCE_MAC || JUCE_LINUX
    if (socketPath.isNotEmpty()) ::unlink(socketPath.toRawUTF8());
   #endif
    getConfigurationFile().deleteFile();
}

juce::Result PerformanceSurfaceBridge::writeConfiguration() const
{
    auto root = juce::var(new juce::DynamicObject());
    auto* object = root.getDynamicObject();
    object->setProperty("schema", 1);
    object->setProperty("transport", "unix");
    object->setProperty("socketPath", socketPath);
    object->setProperty("token", sessionToken);
    const auto file = getConfigurationFile();
    const auto temporary = file.getSiblingFile(file.getFileName() + ".tmp");
    if (! temporary.replaceWithText(juce::JSON::toString(root, false)))
        return juce::Result::fail("Cannot publish performance-surface configuration");
   #if JUCE_MAC || JUCE_LINUX
    ::chmod(temporary.getFullPathName().toRawUTF8(), S_IRUSR | S_IWUSR);
   #endif
    if (file.existsAsFile() ? ! temporary.replaceFileIn(file) : ! temporary.moveFileTo(file))
        return juce::Result::fail("Cannot activate performance-surface configuration");
    return juce::Result::ok();
}

juce::String PerformanceSurfaceBridge::dispatchOnMessageThread(const juce::String& request)
{
    struct PendingRequest
    {
        juce::String request;
        juce::String response;
        juce::WaitableEvent complete;
    };
    auto pending = std::make_shared<PendingRequest>();
    pending->request = request;
    juce::MessageManager::callAsync([this, pending]
    {
        PerformanceSurfaceProtocol protocol(dataModel, macros, sessionToken);
        pending->response = protocol.handleCommand(pending->request);
        pending->complete.signal();
    });
    return pending->complete.wait(1500) ? pending->response
                                        : errorPayload("StudioForge did not respond to the control-surface request");
}

void PerformanceSurfaceBridge::run()
{
   #if JUCE_MAC || JUCE_LINUX
    while (! threadShouldExit() && accepting.load(std::memory_order_acquire))
    {
        if (listeningSocket < 0) break;
        fd_set readable;
        FD_ZERO(&readable);
        FD_SET(listeningSocket, &readable);
        timeval timeout { 0, 200000 };
        if (::select(listeningSocket + 1, &readable, nullptr, nullptr, &timeout) <= 0)
            continue;

        const auto client = ::accept(listeningSocket, nullptr, nullptr);
        if (client < 0) continue;
        char buffer[65537] {};
        const auto bytes = ::recv(client, buffer, 65536, 0);
        if (bytes > 0)
        {
            auto request = juce::String::fromUTF8(buffer, static_cast<int>(bytes)).upToFirstOccurrenceOf("\n", false, false);
            const auto response = dispatchOnMessageThread(request) + "\n";
            ::send(client, response.toRawUTF8(), static_cast<size_t>(response.getNumBytesAsUTF8()), 0);
        }
        ::shutdown(client, SHUT_RDWR);
        ::close(client);
    }
   #endif
}
