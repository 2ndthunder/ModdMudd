#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace mm
{

/** The list of installed VST3 plug-ins, shared by every ModdMudd instance in
    the process.

    Scanning runs on the message thread one file per timer tick, so the DAW
    stays responsive between plug-ins. JUCE's dead-man's-pedal file records the
    plug-in being scanned; if it crashes the DAW, it is skipped (blacklisted)
    on the next scan instead of crashing again. */
class PluginCatalog final : public juce::ChangeBroadcaster,
                            private juce::Timer
{
public:
    PluginCatalog();
    ~PluginCatalog() override;

    juce::AudioPluginFormatManager& getFormatManager() noexcept   { return formatManager; }
    juce::KnownPluginList& getKnownList() noexcept                { return knownList; }

    /** Plug-ins that can be loaded, excluding ModdMudd itself. */
    std::vector<juce::PluginDescription> getPlugins (bool instruments) const;

    void startScan (bool rescanEverything);
    void cancelScan();
    bool isScanning() const noexcept               { return scanner != nullptr; }
    juce::String getScanStatus() const             { return scanStatus; }
    float getScanProgress() const noexcept         { return scanProgress; }

    juce::FileSearchPath getSearchPath() const     { return searchPath; }
    void setSearchPath (const juce::FileSearchPath& path);

    /** Message thread. */
    std::unique_ptr<juce::AudioPluginInstance> createInstance (const juce::PluginDescription& d, double sampleRate,
                                                               int blockSize, juce::String& error);

    /** Finds the locally installed plug-in matching a saved description.
        Matching uses the VST3 class ID, not the file path, so sessions and
        presets move between machines and platforms. */
    std::optional<juce::PluginDescription> findMatching (const juce::PluginDescription& saved) const;

    static bool isSelf (const juce::PluginDescription& d);
    static juce::File getDataDirectory();

private:
    void timerCallback() override;
    void loadFromDisk();
    void saveToDisk() const;

    juce::AudioPluginFormatManager formatManager;
    juce::KnownPluginList knownList;
    juce::FileSearchPath searchPath;

    std::unique_ptr<juce::PluginDirectoryScanner> scanner;
    juce::String scanStatus;
    float scanProgress = 0.0f;
    bool rescanAll = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginCatalog)
};

} // namespace mm
