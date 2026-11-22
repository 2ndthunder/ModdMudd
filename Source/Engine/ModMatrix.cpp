#include "ModMatrix.h"
#include <algorithm>
#include <queue>

namespace mm
{

static bool isValid (const Connection& c)
{
    if (c.source < 0 || c.source >= kNumSources)
        return false;

    if (c.dest.internal)
        return c.dest.internalId >= 0 && c.dest.internalId < kNumInternalParams;

    return c.dest.slotUid.isNotEmpty() && c.dest.paramId.isNotEmpty();
}

std::vector<std::pair<int, int>> ModMatrix::buildEdges (const Connection* extra, int skipId) const
{
    std::vector<std::pair<int, int>> edges;

    auto addEdge = [&edges] (const Connection& c)
    {
        if (! c.dest.internal)
            return;

        const int owner = ownerNodeForInternal (c.dest.internalId);
        if (owner >= 0)
            edges.emplace_back (nodeForSource (c.source), owner);
    };

    for (auto& c : connections)
        if (c.id != skipId)
            addEdge (c);

    if (extra != nullptr)
        addEdge (*extra);

    return edges;
}

bool ModMatrix::hasCycle (const std::vector<std::pair<int, int>>& edges)
{
    std::array<int, kNumNodes> indegree {};
    std::array<std::vector<int>, kNumNodes> out;

    for (auto [from, to] : edges)
    {
        if (from == to)
            return true;

        out[(size_t) from].push_back (to);
        ++indegree[(size_t) to];
    }

    std::queue<int> ready;
    for (int i = 0; i < kNumNodes; ++i)
        if (indegree[(size_t) i] == 0)
            ready.push (i);

    int visited = 0;
    while (! ready.empty())
    {
        const int n = ready.front();
        ready.pop();
        ++visited;

        for (int m : out[(size_t) n])
            if (--indegree[(size_t) m] == 0)
                ready.push (m);
    }

    return visited != kNumNodes;
}

bool ModMatrix::wouldCreateCycle (int source, int internalId) const
{
    Connection probe;
    probe.source = source;
    probe.dest = Destination::internalParam (internalId);
    return hasCycle (buildEdges (&probe, -1));
}

bool ModMatrix::computeNodeOrder (std::array<int, kNumNodes>& order) const
{
    const auto edges = buildEdges (nullptr, -1);

    std::array<int, kNumNodes> indegree {};
    std::array<std::vector<int>, kNumNodes> out;

    for (auto [from, to] : edges)
    {
        if (from == to)
            return false;

        out[(size_t) from].push_back (to);
        ++indegree[(size_t) to];
    }

    // Kahn's algorithm, always taking the lowest-numbered ready node so the
    // order is stable.
    std::vector<int> ready;
    for (int i = 0; i < kNumNodes; ++i)
        if (indegree[(size_t) i] == 0)
            ready.push_back (i);

    int count = 0;
    while (! ready.empty())
    {
        std::sort (ready.begin(), ready.end(), std::greater<int>());
        const int n = ready.back();
        ready.pop_back();
        order[(size_t) count++] = n;

        for (int m : out[(size_t) n])
            if (--indegree[(size_t) m] == 0)
                ready.push_back (m);
    }

    return count == kNumNodes;
}

ModMatrix::AddResult ModMatrix::add (Connection c, int* newId)
{
    if (! isValid (c))
        return AddResult::invalid;

    for (auto& existing : connections)
        if (existing.source == c.source && existing.lane == c.lane && existing.dest == c.dest)
            return AddResult::duplicate;

    if (c.dest.internal && wouldCreateCycle (c.source, c.dest.internalId))
        return AddResult::cycle;

    c.id = nextId++;
    connections.push_back (c);

    if (newId != nullptr)
        *newId = c.id;

    return AddResult::added;
}

bool ModMatrix::remove (int connectionId)
{
    auto it = std::find_if (connections.begin(), connections.end(), [=] (auto& c) { return c.id == connectionId; });
    if (it == connections.end())
        return false;

    connections.erase (it);
    return true;
}

void ModMatrix::removeIf (std::function<bool (const Connection&)> predicate)
{
    connections.erase (std::remove_if (connections.begin(), connections.end(), predicate), connections.end());
}

bool ModMatrix::update (const Connection& c)
{
    for (auto& existing : connections)
    {
        if (existing.id != c.id)
            continue;

        if (! isValid (c))
            return false;

        if (c.dest.internal && hasCycle (buildEdges (&c, c.id)))
            return false;

        existing = c;
        return true;
    }

    return false;
}

void ModMatrix::clear()
{
    connections.clear();
    settings.clear();
}

const Connection* ModMatrix::find (int id) const
{
    for (auto& c : connections)
        if (c.id == id)
            return &c;

    return nullptr;
}

std::vector<const Connection*> ModMatrix::getConnectionsTo (const Destination& d) const
{
    std::vector<const Connection*> result;
    const auto k = d.key();

    for (auto& c : connections)
        if (c.dest.key() == k)
            result.push_back (&c);

    return result;
}

DestinationSettings ModMatrix::getSettings (const Destination& d) const
{
    auto it = settings.find (d.key());
    return it != settings.end() ? it->second : DestinationSettings {};
}

void ModMatrix::setSettings (const Destination& d, const DestinationSettings& s)
{
    DestinationSettings clean = s;
    clean.rangeMin = juce::jlimit (0.0f, 1.0f, clean.rangeMin);
    clean.rangeMax = juce::jlimit (clean.rangeMin, 1.0f, clean.rangeMax);
    clean.smoothingMs = juce::jlimit (0.0f, 2000.0f, clean.smoothingMs);
    settings[d.key()] = clean;
}

int ModMatrix::nextLaneFor (int source) const
{
    int count = 0;
    for (auto& c : connections)
        if (c.source == source)
            ++count;

    return count % kRandomLanes;
}

//==============================================================================
static const juce::Identifier idMatrix ("Matrix"), idConn ("Conn"), idDest ("Dest"),
                              idSource ("source"), idLane ("lane"), idInternal ("internal"),
                              idInternalId ("internalId"), idSlot ("slot"), idParam ("param"),
                              idDepth ("depth"), idOffset ("offset"), idPolarity ("polarity"),
                              idEnabled ("enabled"), idKey ("key"), idMin ("min"), idMax ("max"),
                              idSmooth ("smooth");

juce::ValueTree ModMatrix::toValueTree() const
{
    juce::ValueTree tree (idMatrix);

    for (auto& c : connections)
    {
        juce::ValueTree t (idConn);
        t.setProperty (idSource, c.source, nullptr);
        t.setProperty (idLane, c.lane, nullptr);
        t.setProperty (idInternal, c.dest.internal, nullptr);
        t.setProperty (idInternalId, c.dest.internalId, nullptr);
        t.setProperty (idSlot, c.dest.slotUid, nullptr);
        t.setProperty (idParam, c.dest.paramId, nullptr);
        t.setProperty (idDepth, c.depth, nullptr);
        t.setProperty (idOffset, c.offset, nullptr);
        t.setProperty (idPolarity, (int) c.polarity, nullptr);
        t.setProperty (idEnabled, c.enabled, nullptr);
        tree.appendChild (t, nullptr);
    }

    for (auto& [key, s] : settings)
    {
        juce::ValueTree t (idDest);
        t.setProperty (idKey, key, nullptr);
        t.setProperty (idMin, s.rangeMin, nullptr);
        t.setProperty (idMax, s.rangeMax, nullptr);
        t.setProperty (idSmooth, s.smoothingMs, nullptr);
        tree.appendChild (t, nullptr);
    }

    return tree;
}

void ModMatrix::fromValueTree (const juce::ValueTree& tree)
{
    clear();

    if (! tree.hasType (idMatrix))
        return;

    for (auto t : tree)
    {
        if (t.hasType (idConn))
        {
            Connection c;
            c.source        = t.getProperty (idSource, 0);
            c.lane          = t.getProperty (idLane, 0);
            c.dest.internal = t.getProperty (idInternal, false);
            c.dest.internalId = t.getProperty (idInternalId, -1);
            c.dest.slotUid  = t.getProperty (idSlot).toString();
            c.dest.paramId  = t.getProperty (idParam).toString();
            c.depth         = juce::jlimit (-1.0f, 1.0f, (float) t.getProperty (idDepth, 0.5f));
            c.offset        = juce::jlimit (-1.0f, 1.0f, (float) t.getProperty (idOffset, 0.0f));
            c.polarity      = (Polarity) juce::jlimit (0, 2, (int) t.getProperty (idPolarity, 0));
            c.enabled       = t.getProperty (idEnabled, true);
            add (c);   // re-validates, so a corrupted file can't introduce a loop
        }
        else if (t.hasType (idDest))
        {
            DestinationSettings s;
            s.rangeMin    = t.getProperty (idMin, 0.0f);
            s.rangeMax    = t.getProperty (idMax, 1.0f);
            s.smoothingMs = t.getProperty (idSmooth, 5.0f);
            settings[t.getProperty (idKey).toString()] = s;
        }
    }
}

} // namespace mm
