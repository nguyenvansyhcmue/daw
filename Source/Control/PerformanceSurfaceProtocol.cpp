#include "PerformanceSurfaceProtocol.h"

namespace
{
juce::var makeObject()
{
    return juce::var(new juce::DynamicObject());
}

juce::DynamicObject* objectFor(juce::var& value)
{
    return value.getDynamicObject();
}

juce::String stringProperty(const juce::DynamicObject& object, const char* name)
{
    return object.getProperty(name).toString();
}

bool readInteger(const juce::DynamicObject& object, const char* name, int& value)
{
    const auto source = object.getProperty(name);
    if (! source.isInt() && ! source.isInt64()) return false;
    value = static_cast<int>(source);
    return true;
}

bool readRevision(const juce::DynamicObject& object, uint64_t& value)
{
    const auto source = object.getProperty("projectRevision").toString().trim();
    if (source.isEmpty()) return false;
    try { value = static_cast<uint64_t>(source.getLargeIntValue()); }
    catch (...) { return false; }
    return true;
}
}

PerformanceSurfaceProtocol::PerformanceSurfaceProtocol(TrackDataModel& model,
                                                       PerformanceMacroService& macros,
                                                       juce::String sessionToken)
    : dataModel(model), macroService(macros), token(std::move(sessionToken))
{
}

juce::var PerformanceSurfaceProtocol::snapshotToVar(const PerformanceMacroService::Snapshot& snapshot)
{
    auto value = makeObject();
    auto* object = objectFor(value);
    object->setProperty("macroId", static_cast<int>(snapshot.macroIndex));
    object->setProperty("macroName", snapshot.displayName);
    object->setProperty("value", snapshot.value);
    object->setProperty("state", static_cast<int>(snapshot.state));
    object->setProperty("busId", juce::String(snapshot.bus.value));
    object->setProperty("busName", snapshot.busName);
    object->setProperty("pluginSlot", snapshot.pluginSlot);
    object->setProperty("pluginInstanceId", snapshot.pluginIdentifier);
    object->setProperty("pluginName", snapshot.pluginName);
    object->setProperty("confidence", snapshot.mapping.confidence);
    object->setProperty("needsReview", snapshot.mapping.needsReview);

    juce::Array<juce::var> targets;
    for (const auto& target : snapshot.targets)
    {
        auto targetValue = makeObject();
        auto* targetObject = objectFor(targetValue);
        targetObject->setProperty("parameterId", target.parameterIdentifier);
        targetObject->setProperty("name", target.displayName);
        targetObject->setProperty("semantic", static_cast<int>(target.semantic));
        targetObject->setProperty("valueText", target.valueText);
        targets.add(std::move(targetValue));
    }
    object->setProperty("targets", std::move(targets));
    return value;
}

juce::String PerformanceSurfaceProtocol::makeStatePayload() const
{
    auto root = makeObject();
    auto* object = objectFor(root);
    object->setProperty("type", "performanceMacroState");
    object->setProperty("projectRevision", juce::String(dataModel.getProjectRevision()));
    juce::Array<juce::var> macros;
    for (size_t index = 0; index < PerformanceMacroService::macroCount; ++index)
        macros.add(snapshotToVar(macroService.getSnapshot(index)));
    object->setProperty("macros", std::move(macros));
    juce::Array<juce::var> buses;
    for (const auto& bus : macroService.getBusOptions())
    {
        auto busValue = makeObject();
        auto* busObject = objectFor(busValue);
        busObject->setProperty("id", juce::String(bus.id.value));
        busObject->setProperty("name", bus.name);
        juce::Array<juce::var> plugins;
        for (const auto& plugin : macroService.getPluginOptions(bus.id))
        {
            auto pluginValue = makeObject();
            auto* pluginObject = objectFor(pluginValue);
            pluginObject->setProperty("slot", plugin.slot);
            pluginObject->setProperty("instanceId", plugin.identifier);
            pluginObject->setProperty("name", plugin.name);
            pluginObject->setProperty("bypassed", plugin.bypassed);
            plugins.add(std::move(pluginValue));
        }
        busObject->setProperty("plugins", std::move(plugins));
        buses.add(std::move(busValue));
    }
    object->setProperty("buses", std::move(buses));
    return juce::JSON::toString(root, false);
}

juce::String PerformanceSurfaceProtocol::makeError(const juce::String& message) const
{
    auto root = makeObject();
    auto* object = objectFor(root);
    object->setProperty("ok", false);
    object->setProperty("error", message);
    object->setProperty("projectRevision", juce::String(dataModel.getProjectRevision()));
    return juce::JSON::toString(root, false);
}

juce::String PerformanceSurfaceProtocol::makeSnapshotPayload(const PerformanceMacroService::Snapshot& snapshot) const
{
    auto root = makeObject();
    auto* object = objectFor(root);
    object->setProperty("ok", true);
    object->setProperty("projectRevision", juce::String(dataModel.getProjectRevision()));
    object->setProperty("macro", snapshotToVar(snapshot));
    return juce::JSON::toString(root, false);
}

bool PerformanceSurfaceProtocol::validateEnvelope(const juce::DynamicObject& request, juce::String& error) const
{
    if (token.isEmpty() || stringProperty(request, "token") != token)
    {
        error = "Control-surface session is not authorised";
        return false;
    }
    uint64_t revision = 0;
    if (! readRevision(request, revision) || revision != dataModel.getProjectRevision())
    {
        error = "Project changed; refresh the control surface before applying a macro";
        return false;
    }
    return true;
}

juce::String PerformanceSurfaceProtocol::handleCommand(const juce::String& requestJson)
{
    const auto parsed = juce::JSON::parse(requestJson);
    const auto* request = parsed.getDynamicObject();
    if (request == nullptr) return makeError("Control-surface command is not valid JSON");

    const auto command = stringProperty(*request, "command");
    // State reads use the session token but deliberately do not require a
    // revision. A newly opened control surface needs an authoritative first
    // snapshot before it can include the revision in a mutating command.
    if (command == "getState")
    {
        if (token.isEmpty() || stringProperty(*request, "token") != token)
            return makeError("Control-surface session is not authorised");
        return makeStatePayload();
    }

    juce::String error;
    if (! validateEnvelope(*request, error)) return makeError(error);

    int macroId = -1;
    if (! readInteger(*request, "macroId", macroId) || macroId < 0
        || macroId >= static_cast<int>(PerformanceMacroService::macroCount))
        return makeError("Macro identifier is invalid");

    if (command == "clearMacroMapping")
    {
        const auto result = macroService.clear(static_cast<size_t>(macroId));
        return result.succeeded ? makeSnapshotPayload(result.snapshot) : makeError(result.error);
    }
    if (command == "setMacroValue")
    {
        const auto value = request->getProperty("value");
        if (! value.isDouble() && ! value.isInt()) return makeError("Macro value is invalid");
        const auto result = macroService.setValue(static_cast<size_t>(macroId), static_cast<float>(value));
        return result.succeeded ? makeSnapshotPayload(result.snapshot) : makeError(result.error);
    }

    int pluginSlot = -1;
    const auto busText = stringProperty(*request, "busId").trim();
    const auto busValue = static_cast<uint64_t>(busText.getLargeIntValue());
    if (busText.isEmpty() || busValue == 0 || ! readInteger(*request, "pluginSlot", pluginSlot))
        return makeError("Bus or plug-in slot is invalid");
    const BusId bus { busValue };
    const auto preview = macroService.preview(bus, pluginSlot);
    if (! preview.available) return makeError(preview.error);

    const auto expectedPlugin = stringProperty(*request, "pluginInstanceId");
    if (expectedPlugin.isEmpty() || expectedPlugin != preview.pluginIdentifier)
        return makeError("Plug-in target changed; refresh the mapping panel");

    if (command == "previewMacroMapping" || command == "requestSmartMapping")
    {
        PerformanceMacroService::Snapshot snapshot;
        snapshot.macroIndex = static_cast<size_t>(macroId);
        snapshot.bus = preview.bus;
        snapshot.pluginSlot = preview.pluginSlot;
        snapshot.busName = preview.busName;
        snapshot.pluginIdentifier = preview.pluginIdentifier;
        snapshot.pluginName = preview.pluginName;
        snapshot.displayName = preview.proposal.displayName;
        snapshot.mapping = preview.proposal;
        for (const auto& target : preview.proposal.targets)
            snapshot.targets.push_back({ target.parameterIdentifier, target.displayName, target.semantic, {} });
        snapshot.state = preview.bypassed ? PerformanceMacroService::State::bypassed
                                          : (preview.proposal.needsReview ? PerformanceMacroService::State::needsReview
                                                                         : PerformanceMacroService::State::active);
        return makeSnapshotPayload(snapshot);
    }
    if (command == "applyMacroMapping")
    {
        const auto result = macroService.apply(static_cast<size_t>(macroId), bus, pluginSlot,
                                               static_cast<bool>(request->getProperty("confirmNeedsReview")));
        return result.succeeded ? makeSnapshotPayload(result.snapshot) : makeError(result.error);
    }
    return makeError("Unknown control-surface command");
}
