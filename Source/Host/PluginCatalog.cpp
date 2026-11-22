#include "PluginCatalog.h"

namespace mm
{

static juce::AudioPluginFormat* findVst3 (juce::AudioPluginFormatManager& m)
{
    for (auto* f : m.getFormats())
        if (f->getName() == "VST3")
            return f;

    return nullptr;
}

PluginCatalog::PluginCatalog()
{
    // VST3 is the only hosted format. Other formats are compiled out.
    formatManager.addFormat (std::make_unique<juce::VST3PluginFormat>());

    if (auto* vst3 = findVst3 (formatManager))
        searchPath = vst3->getDefaultLocationsToSearch();

    loadFromDisk();
}

PluginCatalog::~PluginCatalog()
{
    stopTimer();
    scanner.reset();
}

juce::File PluginCatalog::getDataDirectory()
{
   #if JUCE_MAC
    auto dir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("Application Support").getChildFile ("ModdMudd");
   #else
    auto dir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("ModdMudd");
   #endif
    dir.createDirectory();
    return dir;
}

bool PluginCatalog::isSelf (const juce::PluginDescription& d)
{
    // Loading ModdMudd inside itself would recurse; never offer it.
    return d.manufacturerName.equalsIgnoreCase ("ModdMudd") || d.name.startsWithIgnoreCase ("ModdMudd");
}

std::vector<juce::PluginDescription> PluginCatalog::getPlugins (bool instruments) const
{
    std::vector<juce::PluginDescription> result;

    for (auto& d : knownList.getTypes())
        if (d.pluginFormatName == "VST3" && d.isInstrument == instruments && ! isSelf (d))
            result.push_back (d);

    std::sort (result.begin(), result.end(), [] (auto& a, auto& b)
    {
        const int m = a.manufacturerName.compareIgnoreCase (b.manufacturerName);
        return m != 0 ? m < 0 : a.name.compareIgnoreCase (b.name) < 0;
    });

    return result;
}

void PluginCatalog::setSearchPath (const juce::FileSearchPath& path)
{
    searchPath = path;
    saveToDisk();
}

void PluginCatalog::startScan (bool rescanEverything)
{
    auto* vst3 = findVst3 (formatManager);
    if (vst3 == nullptr || scanner != nullptr)
        return;

    rescanAll = rescanEverything;

    if (rescanEverything)
        knownList.clear();

    scanner = std::make_unique<juce::PluginDirectoryScanner> (knownList, *vst3, searchPath, true,
                                                              getDataDirectory().getChildFile ("scan-in-progress.txt"),
                                                              false);
    scanStatus = "Scanning...";
    scanProgress = 0.0f;
    sendChangeMessage();
    startTimer (20);
}

void PluginCatalog::cancelScan()
{
    stopTimer();
    scanner.reset();
    scanStatus = "Scan cancelled";
    saveToDisk();
    sendChangeMessage();
}

void PluginCatalog::timerCallback()
{
    if (scanner == nullptr)
    {
        stopTimer();
        return;
    }

    juce::String name;
    const bool more = scanner->scanNextFile (! rescanAll, name);
    scanProgress = scanner->getProgress();
    scanStatus = "Scanning " + name;

    if (! more)
    {
        stopTimer();

        const auto failed = scanner->getFailedFiles();
        scanner.reset();

        scanStatus = "Found " + juce::String (getPlugins (false).size() + getPlugins (true).size()) + " plug-ins";
        if (! failed.isEmpty())
            scanStatus << ", " << failed.size() << " failed";

        scanProgress = 1.0f;
        saveToDisk();
    }

    sendChangeMessage();
}

std::unique_ptr<juce::AudioPluginInstance> PluginCatalog::createInstance (const juce::PluginDescription& d, double sampleRate,
                                                                          int blockSize, juce::String& error)
{
    JUCE_ASSERT_MESSAGE_THREAD

    if (isSelf (d))
    {
        error = "ModdMudd can't host itself";
        return nullptr;
    }

    return formatManager.createPluginInstance (d, sampleRate, blockSize, error);
}

std::optional<juce::PluginDescription> PluginCatalog::findMatching (const juce::PluginDescription& saved) const
{
    const auto types = knownList.getTypes();

    // 1. Same VST3 class (uniqueId is derived from the class ID).
    for (auto& d : types)
        if (d.uniqueId == saved.uniqueId && d.uniqueId != 0 && d.name == saved.name)
            return d;

    for (auto& d : types)
        if (d.uniqueId == saved.uniqueId && d.uniqueId != 0)
            return d;

    // 2. Same name and manufacturer.
    for (auto& d : types)
        if (d.name == saved.name && d.manufacturerName == saved.manufacturerName && d.isInstrument == saved.isInstrument)
            return d;

    // 3. The saved file is present on this machine but hasn't been scanned.
    //    Older sessions may name the binary inside the bundle; use the bundle.
    if (juce::File::isAbsolutePath (saved.fileOrIdentifier))
    {
        auto file = juce::File (saved.fileOrIdentifier);

        for (auto f = file; f.getFullPathName().length() > 1; f = f.getParentDirectory())
            if (f.hasFileExtension ("vst3"))
            {
                file = f;
                break;
            }

        if (file.exists())
        {
            auto d = saved;
            d.fileOrIdentifier = file.getFullPathName();
            return d;
        }
    }

    return std::nullopt;
}

void PluginCatalog::loadFromDisk()
{
    const auto file = getDataDirectory().getChildFile ("plugins.xml");

    if (auto xml = juce::XmlDocument::parse (file))
    {
        if (auto* list = xml->getChildByName ("KNOWNPLUGINS"))
            knownList.recreateFromXml (*list);

        if (auto* paths = xml->getChildByName ("SEARCHPATH"))
            searchPath = juce::FileSearchPath (paths->getStringAttribute ("paths", searchPath.toString()));
    }
}

void PluginCatalog::saveToDisk() const
{
    juce::XmlElement root ("MODDMUDD_PLUGINS");

    if (auto list = knownList.createXml())
        root.addChildElement (list.release());

    auto* paths = root.createNewChildElement ("SEARCHPATH");
    paths->setAttribute ("paths", searchPath.toString());

    root.writeTo (getDataDirectory().getChildFile ("plugins.xml"));
}

} // namespace mm
