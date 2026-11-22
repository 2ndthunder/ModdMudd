#pragma once

#include "ModTypes.h"
#include <juce_data_structures/juce_data_structures.h>
#include <functional>
#include <map>
#include <optional>

namespace mm
{

/** Where a connection sends its modulation. */
struct Destination
{
    bool         internal   = false;
    int          internalId = -1;   // InternalParam when internal
    juce::String slotUid;           // hosted plug-in slot
    juce::String paramId;           // hosted parameter ID (stable across sessions)

    static Destination hosted (const juce::String& slot, const juce::String& param)
    {
        Destination d;
        d.slotUid = slot;
        d.paramId = param;
        return d;
    }

    static Destination internalParam (int id)
    {
        Destination d;
        d.internal = true;
        d.internalId = id;
        return d;
    }

    juce::String key() const
    {
        return internal ? "i:" + juce::String (internalId) : "h:" + slotUid + ":" + paramId;
    }

    bool operator== (const Destination& o) const   { return key() == o.key(); }
};

struct Connection
{
    int         id       = 0;
    int         source   = 0;
    int         lane     = 0;
    Destination dest;
    float       depth    = 0.5f;
    float       offset   = 0.0f;
    Polarity    polarity = Polarity::natural;
    bool        enabled  = true;
};

/** Per-destination settings shared by all connections to it. */
struct DestinationSettings
{
    float rangeMin    = 0.0f;
    float rangeMax    = 1.0f;
    float smoothingMs = 5.0f;
};

/** The editable modulation routing. Message thread only. */
class ModMatrix
{
public:
    enum class AddResult { added, duplicate, cycle, invalid };

    /** Adds a connection unless it would create a feedback loop between
        modulators, or the same source already drives the destination. */
    AddResult add (Connection c, int* newId = nullptr);

    bool remove (int connectionId);
    void removeIf (std::function<bool (const Connection&)> predicate);
    bool update (const Connection& c);   // fails if the update would create a cycle
    void clear();

    const std::vector<Connection>& getConnections() const noexcept   { return connections; }
    const Connection* find (int id) const;
    std::vector<const Connection*> getConnectionsTo (const Destination& d) const;

    DestinationSettings getSettings (const Destination& d) const;
    void setSettings (const Destination& d, const DestinationSettings& s);

    /** True if routing `source` to internal parameter `internalId` would make a loop. */
    bool wouldCreateCycle (int source, int internalId) const;

    /** Evaluation order for modulator nodes so every node runs after the
        nodes that modulate it. Returns false if the graph has a cycle. */
    bool computeNodeOrder (std::array<int, kNumNodes>& order) const;

    int nextLaneFor (int source) const;

    juce::ValueTree toValueTree() const;
    void fromValueTree (const juce::ValueTree& tree);

private:
    static bool hasCycle (const std::vector<std::pair<int, int>>& edges);
    std::vector<std::pair<int, int>> buildEdges (const Connection* extra, int skipId) const;

    std::vector<Connection> connections;
    std::map<juce::String, DestinationSettings> settings;
    int nextId = 1;
};

} // namespace mm
