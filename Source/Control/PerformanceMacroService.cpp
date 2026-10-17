#include "PerformanceMacroService.h"

#include <algorithm>
#include <cmath>

PerformanceMacroService::PerformanceMacroService(TrackDataModel& model)
    : dataModel(model)
{
    for (size_t index = 0; index < macros.size(); ++index)
        macros[index].macroIndex = index;
    dataModel.addChangeListener(this);
}

PerformanceMacroService::~PerformanceMacroService()
{
    dataModel.removeChangeListener(this);
}

bool PerformanceMacroService::isValidMacroIndex(size_t macroIndex) noexcept
{
    return macroIndex < macroCount;
}

juce::String PerformanceMacroService::identifierFor(const AudioEffectProcessor& processor)
{
    const auto persistent = processor.getPersistentIdentifier();
    return persistent.isNotEmpty() ? persistent : processor.getName();
}

std::vector<PerformanceMacroService::BusOption> PerformanceMacroService::getBusOptions() const
{
    std::vector<BusOption> options;
    const auto& buses = dataModel.getBuses();
    options.reserve(buses.size());
    for (const auto& bus : buses)
        options.push_back({ bus.id, bus.name });
    return options;
}

std::vector<PerformanceMacroService::PluginOption> PerformanceMacroService::getPluginOptions(BusId bus) const
{
    std::vector<PluginOption> options;
    const auto* rack = dataModel.getBusFxRackSnapshot(bus);
    if (rack == nullptr) return options;
    for (size_t slot = 0; slot < rack->processors.size(); ++slot)
    {
        const auto& processor = rack->processors[slot];
        if (processor == nullptr) continue;
        options.push_back({ static_cast<int>(slot), identifierFor(*processor), processor->getName(), rack->bypass[slot] });
    }
    return options;
}

PerformanceMacroService::Preview PerformanceMacroService::preview(BusId bus, int pluginSlot) const
{
    Preview result;
    result.bus = bus;
    result.pluginSlot = pluginSlot;
    if (! bus.isValid())
    {
        result.error = "Choose a bus before choosing a plug-in";
        return result;
    }
    if (pluginSlot < 0 || pluginSlot >= static_cast<int>(TrackDataModel::maxFxSlots))
    {
        result.error = "The selected plug-in slot is outside this bus rack";
        return result;
    }

    const auto& buses = dataModel.getBuses();
    const auto foundBus = std::find_if(buses.begin(), buses.end(), [bus] (const auto& state)
    {
        return state.id == bus;
    });
    if (foundBus == buses.end())
    {
        result.error = "The selected bus no longer exists";
        return result;
    }
    result.busName = foundBus->name;

    const auto* rack = dataModel.getBusFxRackSnapshot(bus);
    if (rack == nullptr || rack->processors[static_cast<size_t>(pluginSlot)] == nullptr)
    {
        result.error = "The selected bus slot is empty";
        return result;
    }

    const auto& processor = *rack->processors[static_cast<size_t>(pluginSlot)];
    result.available = true;
    result.bypassed = rack->bypass[static_cast<size_t>(pluginSlot)];
    result.pluginIdentifier = identifierFor(processor);
    result.pluginName = processor.getName();
    result.proposal = PluginSemanticMapper::propose(result.pluginName, processor.getParameterDescriptors());
    return result;
}

PerformanceMacroService::Result PerformanceMacroService::apply(size_t macroIndex, BusId bus, int pluginSlot,
                                                                 bool confirmNeedsReview)
{
    Result result;
    if (! isValidMacroIndex(macroIndex))
    {
        result.error = "Unknown performance macro";
        return result;
    }

    const auto candidate = preview(bus, pluginSlot);
    if (! candidate.available)
    {
        result.error = candidate.error;
        return result;
    }
    if (candidate.proposal.targets.empty())
    {
        result.error = "This plug-in has no safe smart mapping";
        return result;
    }
    if (candidate.proposal.needsReview && ! confirmNeedsReview)
    {
        result.error = "Smart mapping needs explicit confirmation";
        return result;
    }

    auto& macro = macros[macroIndex];
    macro.bus = candidate.bus;
    macro.pluginSlot = candidate.pluginSlot;
    macro.busName = candidate.busName;
    macro.pluginIdentifier = candidate.pluginIdentifier;
    macro.pluginName = candidate.pluginName;
    macro.displayName = candidate.proposal.displayName;
    macro.mapping = candidate.proposal;
    macro.targets.clear();
    // A low-confidence proposal remains preview-only until the caller passes
    // confirmNeedsReview above. Once explicitly approved it is an active
    // mapping, not a perpetually unsafe one that can still receive values.
    macro.state = candidate.bypassed ? State::bypassed : State::active;
    refreshSnapshot(macro);
    result.succeeded = true;
    result.snapshot = macro;
    notifyChanged();
    return result;
}

PerformanceMacroService::Result PerformanceMacroService::clear(size_t macroIndex)
{
    Result result;
    if (! isValidMacroIndex(macroIndex))
    {
        result.error = "Unknown performance macro";
        return result;
    }
    macros[macroIndex] = {};
    macros[macroIndex].macroIndex = macroIndex;
    result.succeeded = true;
    result.snapshot = macros[macroIndex];
    notifyChanged();
    return result;
}

PerformanceMacroService::Result PerformanceMacroService::setValue(size_t macroIndex, float normalisedValue)
{
    Result result;
    if (! isValidMacroIndex(macroIndex))
    {
        result.error = "Unknown performance macro";
        return result;
    }

    auto& macro = macros[macroIndex];
    refreshSnapshot(macro);
    if (macro.state != State::active)
    {
        result.error = "This macro has no active plug-in target";
        result.snapshot = macro;
        return result;
    }

    const auto* rack = dataModel.getBusFxRackSnapshot(macro.bus);
    if (rack == nullptr || macro.pluginSlot < 0 || macro.pluginSlot >= static_cast<int>(TrackDataModel::maxFxSlots))
    {
        result.error = "The mapped bus is no longer available";
        result.snapshot = macro;
        return result;
    }
    const auto& processor = rack->processors[static_cast<size_t>(macro.pluginSlot)];
    if (processor == nullptr || identifierFor(*processor) != macro.pluginIdentifier)
    {
        result.error = "The mapped plug-in was replaced";
        macro.state = State::unassigned;
        result.snapshot = macro;
        notifyChanged();
        return result;
    }

    const auto value = juce::jlimit(0.0f, 1.0f, normalisedValue);
    for (const auto& target : macro.mapping.targets)
    {
        const auto mapped = target.minimum + (target.maximum - target.minimum) * value;
        if (! processor->setParameterValue(target.parameterIdentifier, mapped))
        {
            result.error = "A mapped plug-in parameter is no longer available";
            macro.state = State::unassigned;
            result.snapshot = macro;
            notifyChanged();
            return result;
        }
    }
    macro.value = value;
    refreshSnapshot(macro);
    result.succeeded = true;
    result.snapshot = macro;
    notifyChanged();
    return result;
}

PerformanceMacroService::Snapshot PerformanceMacroService::getSnapshot(size_t macroIndex) const
{
    if (! isValidMacroIndex(macroIndex)) return {};
    auto snapshot = macros[macroIndex];
    refreshSnapshot(snapshot);
    return snapshot;
}

void PerformanceMacroService::refresh()
{
    auto changed = false;
    for (auto& macro : macros)
    {
        const auto previous = macro;
        refreshSnapshot(macro);
        changed = changed || hasChanged(previous, macro);
    }
    if (changed) notifyChanged();
}

std::vector<PersistedPerformanceMacroState> PerformanceMacroService::createPersistedState() const
{
    std::vector<PersistedPerformanceMacroState> result;
    result.reserve(macros.size());
    for (const auto& source : macros)
    {
        if (source.state == State::unassigned || ! source.bus.isValid())
            continue;

        PersistedPerformanceMacroState saved;
        saved.macroIndex = static_cast<int>(source.macroIndex);
        saved.state = static_cast<int>(source.state);
        saved.value = source.value;
        saved.bus = source.bus;
        saved.pluginSlot = source.pluginSlot;
        saved.displayName = source.displayName;
        saved.pluginIdentifier = source.pluginIdentifier;
        for (const auto& target : source.mapping.targets)
            saved.targets.push_back({ target.parameterIdentifier, target.displayName,
                                      static_cast<int>(target.semantic), target.minimum, target.maximum });
        result.push_back(std::move(saved));
    }
    return result;
}

void PerformanceMacroService::restorePersistedState(const std::vector<PersistedPerformanceMacroState>& state)
{
    for (size_t index = 0; index < macros.size(); ++index)
    {
        macros[index] = {};
        macros[index].macroIndex = index;
    }

    for (const auto& saved : state)
    {
        if (saved.macroIndex < 0 || ! isValidMacroIndex(static_cast<size_t>(saved.macroIndex))
            || ! saved.bus.isValid() || saved.pluginSlot < 0
            || saved.pluginSlot >= static_cast<int>(TrackDataModel::maxFxSlots)
            || saved.pluginIdentifier.isEmpty())
            continue;

        auto& restored = macros[static_cast<size_t>(saved.macroIndex)];
        restored.bus = saved.bus;
        restored.pluginSlot = saved.pluginSlot;
        restored.pluginIdentifier = saved.pluginIdentifier;
        restored.displayName = saved.displayName.isNotEmpty() ? saved.displayName : "Needs Review";
        restored.value = juce::jlimit(0.0f, 1.0f, saved.value);
        restored.state = State::needsReview;
        for (const auto& target : saved.targets)
        {
            if (target.parameterIdentifier.isEmpty() || ! std::isfinite(target.minimum)
                || ! std::isfinite(target.maximum) || target.minimum > target.maximum)
                continue;
            restored.mapping.targets.push_back({ target.parameterIdentifier, target.displayName,
                                                  static_cast<PluginParameterSemantic>(target.semantic),
                                                  target.minimum, target.maximum });
        }

        const auto candidate = preview(saved.bus, saved.pluginSlot);
        if (! candidate.available || candidate.pluginIdentifier != saved.pluginIdentifier)
            continue;

        restored.busName = candidate.busName;
        restored.pluginName = candidate.pluginName;
        const auto descriptors = dataModel.getBusFxRackSnapshot(saved.bus)
                                     ->processors[static_cast<size_t>(saved.pluginSlot)]->getParameterDescriptors();
        const auto parameterExists = [&descriptors] (const juce::String& identifier)
        {
            return std::any_of(descriptors.begin(), descriptors.end(), [&identifier] (const auto& descriptor)
            {
                return descriptor.identifier == identifier;
            });
        };
        const auto allTargetsResolve = ! restored.mapping.targets.empty()
            && std::all_of(restored.mapping.targets.begin(), restored.mapping.targets.end(),
                           [&parameterExists] (const auto& target) { return parameterExists(target.parameterIdentifier); });
        if (allTargetsResolve)
        {
            restored.mapping.confidence = candidate.proposal.confidence;
            restored.mapping.needsReview = static_cast<State>(saved.state) == State::needsReview;
            restored.state = candidate.bypassed ? State::bypassed
                                                : (restored.mapping.needsReview ? State::needsReview : State::active);
        }
        refreshSnapshot(restored);
    }
    notifyChanged();
}

void PerformanceMacroService::refreshSnapshot(Snapshot& snapshot) const
{
    if (! snapshot.bus.isValid() || snapshot.pluginSlot < 0) return;
    const auto candidate = preview(snapshot.bus, snapshot.pluginSlot);
    if (! candidate.available || candidate.pluginIdentifier != snapshot.pluginIdentifier)
    {
        snapshot.state = State::unassigned;
        snapshot.displayName = "Unassigned";
        snapshot.targets.clear();
        return;
    }

    // A persisted, unresolved mapping carries needsReview. An approved
    // low-confidence mapping remains active; confidence is still exposed to
    // the UI so it can show provenance without blocking the performer.
    snapshot.state = candidate.bypassed ? State::bypassed
                                         : (snapshot.state == State::needsReview ? State::needsReview : State::active);
    snapshot.busName = candidate.busName;
    snapshot.pluginName = candidate.pluginName;
    snapshot.targets.clear();

    const auto* rack = dataModel.getBusFxRackSnapshot(snapshot.bus);
    const auto& processor = rack->processors[static_cast<size_t>(snapshot.pluginSlot)];
    for (const auto& target : snapshot.mapping.targets)
    {
        Snapshot::TargetState state;
        state.parameterIdentifier = target.parameterIdentifier;
        state.displayName = target.displayName;
        state.semantic = target.semantic;
        state.valueText = processor->getParameterText(target.parameterIdentifier);
        snapshot.targets.push_back(std::move(state));
    }
}

bool PerformanceMacroService::hasChanged(const Snapshot& before, const Snapshot& after)
{
    if (before.state != after.state || before.displayName != after.displayName
        || before.busName != after.busName || before.pluginName != after.pluginName
        || before.targets.size() != after.targets.size())
        return true;

    for (size_t index = 0; index < before.targets.size(); ++index)
    {
        const auto& lhs = before.targets[index];
        const auto& rhs = after.targets[index];
        if (lhs.parameterIdentifier != rhs.parameterIdentifier || lhs.displayName != rhs.displayName
            || lhs.semantic != rhs.semantic || lhs.valueText != rhs.valueText)
            return true;
    }
    return false;
}

void PerformanceMacroService::changeListenerCallback(juce::ChangeBroadcaster*)
{
    refresh();
}

void PerformanceMacroService::notifyChanged()
{
    ++revision;
    sendChangeMessage();
}
