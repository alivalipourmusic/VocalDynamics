#include <JuceHeader.h>
#include "StandaloneEditor.h"

class VocalDynamicsStandaloneApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "Vocal Dynamics Editor"; }
    const juce::String getApplicationVersion() override { return "0.1.0"; }
    bool moreThanOneInstanceAllowed() override { return true; }

    void initialise(const juce::String&) override
    {
        mainWindow = std::make_unique<MainWindow>(getApplicationName());
    }

    void shutdown() override { mainWindow.reset(); }
    void systemRequestedQuit() override { quit(); }
    void anotherInstanceStarted(const juce::String&) override {}

private:
    class MainWindow : public juce::DocumentWindow
    {
    public:
        explicit MainWindow(const juce::String& name)
            : DocumentWindow(name, juce::Colour(0xff080e17), allButtons)
        {
            setUsingNativeTitleBar(true);
            setContentOwned(new VocalDynamicsStandaloneEditor(), true);
            centreWithSize(1100, 720);
            setResizable(true, true);
            setVisible(true);
        }

        void closeButtonPressed() override
        {
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
        }
    };

    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION(VocalDynamicsStandaloneApplication)
