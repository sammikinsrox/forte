#include "PluginEditor.h"

#include "SoundParameters.h"
#include "editor/LabView.h"
#include "editor/PianoViews.h"
#include "editor/Widgets.h"

#include "forte/Version.h"

namespace forte
{
using View = PluginProcessor::View;
using namespace ui;

namespace
{
/** The credits and notices (SPEC §9.6), over the editor. */
class AboutPanel final : public juce::Component
{
public:
    AboutPanel()
    {
        close.onClick = [this] { setVisible (false); };
        addAndMakeVisible (close);
    }

    void paint (juce::Graphics& g) override
    {
        const auto& t = forteLook (*this).theme();
        g.fillAll (t.bg.withAlpha (0.82f));

        const auto card = cardArea().toFloat();
        paintPanel (g, card, t);

        auto text = card.reduced (32.0f, 24.0f);
        g.setColour (t.ink);
        g.setFont (ForteLookAndFeel::italiana (40.0f, 0.3f));
        g.drawText ("FORTE", text.removeFromTop (46.0f), juce::Justification::centredLeft);
        g.setColour (t.muted);
        g.setFont (ForteLookAndFeel::jost (13.0f));
        g.drawText (juce::String ("Version ") + versionString + ". A physically modelled concert grand.", text.removeFromTop (22.0f),
                    juce::Justification::centredLeft);
        text.removeFromTop (12.0f);

        const char* notices =
            "Forte is free software under the GNU General Public License, version 3 or later.\n\n"
            "Built with JUCE (AGPLv3) and the VST3 SDK (MIT). The Jost and Italiana fonts are used "
            "unmodified under the SIL Open Font License 1.1.\n\n"
            "Fitted from: the University of Iowa Musical Instrument Samples; the Salamander Grand Piano "
            "(CC BY 3.0); Piano in 162; the Plath concert-grand dataset (CC BY 4.0, modified); and the "
            "published work of Chabassier & Durufl\xc3\xa9, Chaigne & Askenfelt, Stulov, Bank et al. and others. "
            "No recordings ship with Forte, only numbers derived from them.\n\n"
            "VST is a registered trademark of Steinberg Media Technologies GmbH.";

        g.setColour (t.ink);
        g.setFont (ForteLookAndFeel::jost (12.5f));
        g.drawFittedText (juce::String::fromUTF8 (notices), text.toNearestInt(), juce::Justification::topLeft, 20, 1.0f);
    }

    void resized() override
    {
        const auto card = cardArea();
        close.setBounds (card.getRight() - 104, card.getBottom() - 46, 80, 28);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! cardArea().contains (e.getPosition()))
            setVisible (false);
    }

private:
    [[nodiscard]] juce::Rectangle<int> cardArea() const { return getLocalBounds().withSizeKeepingCentre (600, 420); }

    juce::TextButton close { "Close" };
};

//==============================================================================
/** Plain text drawn in the theme's muted ink, brighter under the mouse: the preset arrows and
    the preset name. */
class TextLink final : public juce::Button
{
public:
    TextLink (const juce::String& text, float size)
        : juce::Button (text), fontSize (size)
    {
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        const auto& t = forteLook (*this).theme();
        g.setColour (over || down ? t.ink : t.muted);
        g.setFont (ForteLookAndFeel::jost (fontSize).withExtraKerningFactor (0.06f));
        g.drawText (getButtonText(), getLocalBounds(), juce::Justification::centred, true);
    }

private:
    float fontSize;
};

/** The preset name with its ‹ › arrows (SPEC §6.4, §7.1). The arrows step through the factory
    presets, then the user ones, wrapping, and drop unsaved changes. The name opens the menu:
    both lists, Save, Save as…, Rename…, Delete and Open presets folder. */
class PresetNav final : public juce::Component
{
public:
    explicit PresetNav (PresetManager& m)
        : presets (m)
    {
        previous.setTooltip ("Previous preset");
        next.setTooltip ("Next preset");
        name.setTooltip ("Presets: load, save, rename, delete");
        previous.onClick = [this] { presets.step (-1); tick(); };
        next.onClick = [this] { presets.step (1); tick(); };
        name.onClick = [this] { showMenu(); };
        addAndMakeVisible (previous);
        addAndMakeVisible (next);
        addAndMakeVisible (name);
        tick();
    }

    void tick()
    {
        if (const auto shown = presets.displayName(); shown != name.getButtonText())
        {
            name.setButtonText (shown);
            resized();
        }
    }

    void resized() override
    {
        // The arrows hug the name, the three centred in the bounds.
        constexpr int arrowWidth = 22;
        const auto textWidth = juce::GlyphArrangement::getStringWidth (ForteLookAndFeel::jost (14.0f).withExtraKerningFactor (0.06f),
                                                                        name.getButtonText());
        const int nameWidth = juce::jmin (getWidth() - 2 * arrowWidth, juce::roundToInt (textWidth) + 16);
        auto area = getLocalBounds().withSizeKeepingCentre (nameWidth + 2 * arrowWidth, getHeight());
        previous.setBounds (area.removeFromLeft (arrowWidth));
        next.setBounds (area.removeFromRight (arrowWidth));
        name.setBounds (area);
    }

private:
    enum MenuItem
    {
        save = 1,
        saveAs,
        rename,
        remove,
        openFolder,
        firstPreset = 100
    };

    void showMenu()
    {
        auto& lib = presets.library();
        lib.rescan();
        const auto current = presets.current();
        const auto& stops = lib.all();

        juce::PopupMenu menu;
        menu.addSectionHeader ("Factory");
        for (std::size_t i = 0; i < stops.size(); ++i)
            if (stops[i].factory)
                menu.addItem (firstPreset + static_cast<int> (i), stops[i].name, true, stops[i] == current);

        menu.addSectionHeader ("User");
        if (lib.userNames().empty())
            menu.addItem (-1, "None yet: use Save as", false, false);
        for (std::size_t i = 0; i < stops.size(); ++i)
            if (! stops[i].factory)
                menu.addItem (firstPreset + static_cast<int> (i), stops[i].name, true, stops[i] == current);

        menu.addSeparator();
        menu.addItem (save, "Save", ! current.factory);
        menu.addItem (saveAs, "Save as...");
        menu.addItem (rename, "Rename...", ! current.factory);
        menu.addItem (remove, "Delete", ! current.factory && lib.userPresetExists (current.name));
        menu.addSeparator();
        menu.addItem (openFolder, "Open presets folder");

        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&name),
                            [safe = juce::Component::SafePointer<PresetNav> (this), stops] (int choice)
                            {
                                if (safe != nullptr && choice != 0)
                                    safe->choose (choice, stops);
                            });
    }

    void choose (int choice, const std::vector<presets::Id>& stops)
    {
        const auto current = presets.current();

        if (choice >= firstPreset)
        {
            const auto index = static_cast<std::size_t> (choice - firstPreset);
            if (index < stops.size() && ! presets.select (stops[index]))
                report ("That preset is gone", "\"" + stops[index].name + "\" was moved or deleted.");
        }
        else if (choice == save)
        {
            check (presets.save());
        }
        else if (choice == saveAs)
        {
            askName ("Save preset as", current.factory ? juce::String() : current.name, [this] (const juce::String& chosen)
            {
                confirmReplace (chosen, [this, chosen] { check (presets.saveAs (chosen)); });
            });
        }
        else if (choice == rename)
        {
            askName ("Rename preset", current.name, [this, current] (const juce::String& chosen)
            {
                if (chosen == current.name)
                    return;
                const bool sameFile = chosen.equalsIgnoreCase (current.name);
                const auto doIt = [this, chosen] { check (presets.renameCurrent (chosen)); };
                if (sameFile)
                    doIt();
                else
                    confirmReplace (chosen, doIt);
            });
        }
        else if (choice == remove)
        {
            juce::AlertWindow::showAsync (juce::MessageBoxOptions()
                                              .withIconType (juce::MessageBoxIconType::QuestionIcon)
                                              .withTitle ("Delete preset")
                                              .withMessage ("Delete \"" + current.name + "\"? This can't be undone.")
                                              .withButton ("Delete", 1)
                                              .withButton ("Cancel", 0)
                                              .withAssociatedComponent (this),
                                          [safe = juce::Component::SafePointer<PresetNav> (this)] (int button)
                                          {
                                              if (safe != nullptr && button == 1)
                                                  safe->check (safe->presets.deleteCurrent());
                                          });
        }
        else if (choice == openFolder)
        {
            const auto folder = presets.library().getFolder();
            if (const auto made = folder.createDirectory(); made.failed())
                report ("Couldn't open the presets folder", made.getErrorMessage());
            else
                folder.startAsProcess();
        }

        tick();
    }

    /** Asks for a name; calls back with a usable one, never for Cancel or an empty name. */
    void askName (const juce::String& title, const juce::String& initial, std::function<void (const juce::String&)> then)
    {
        auto* window = new juce::AlertWindow (title, "Name:", juce::MessageBoxIconType::NoIcon, this);
        window->addTextEditor ("name", initial);
        window->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
        window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

        window->enterModalState (true, juce::ModalCallbackFunction::create (
            [safe = juce::Component::SafePointer<PresetNav> (this), window, then = std::move (then)] (int button)
            {
                const auto typed = window->getTextEditorContents ("name");
                if (safe == nullptr || button != 1)
                    return;

                const auto clean = presets::Library::cleanName (typed);
                if (clean.isEmpty())
                {
                    safe->report ("No name", "A preset needs a name that can be a file name.");
                    return;
                }
                then (clean);
                safe->tick();
            }), true);
    }

    /** Runs then, after asking first if a user preset of that name would be replaced. */
    void confirmReplace (const juce::String& chosen, std::function<void()> then)
    {
        if (! presets.library().userPresetExists (chosen))
        {
            then();
            return;
        }

        juce::AlertWindow::showAsync (juce::MessageBoxOptions()
                                          .withIconType (juce::MessageBoxIconType::QuestionIcon)
                                          .withTitle ("Replace preset")
                                          .withMessage ("A preset called \"" + chosen + "\" already exists. Replace it?")
                                          .withButton ("Replace", 1)
                                          .withButton ("Cancel", 0)
                                          .withAssociatedComponent (this),
                                      [safe = juce::Component::SafePointer<PresetNav> (this), then = std::move (then)] (int button)
                                      {
                                          if (safe != nullptr && button == 1)
                                          {
                                              then();
                                              safe->tick();
                                          }
                                      });
    }

    void check (const juce::Result& result)
    {
        if (result.failed())
            report ("Presets", result.getErrorMessage());
        tick();
    }

    void report (const juce::String& title, const juce::String& message)
    {
        juce::AlertWindow::showAsync (juce::MessageBoxOptions()
                                          .withIconType (juce::MessageBoxIconType::WarningIcon)
                                          .withTitle (title)
                                          .withMessage (message)
                                          .withButton ("OK", 0)
                                          .withAssociatedComponent (this),
                                      nullptr);
    }

    PresetManager& presets;
    TextLink previous { juce::String::fromUTF8 ("\xe2\x80\xb9"), 20.0f };
    TextLink next { juce::String::fromUTF8 ("\xe2\x80\xba"), 20.0f };
    TextLink name { {}, 14.0f };
};
} // namespace

//==============================================================================
/** Everything inside the window, at the base size: the header and the current view. */
class PluginEditor::Content final : public juce::Component
{
public:
    Content (PluginProcessor& p, PluginEditor& e)
        : processor (p), editor (e)
    {
        tabs.setTooltips ({ "The three dials and the keyboard", "Every control, and the tuning curve", "The voicing lab" });
        tabs.onChange = [this] (int index) { editor.showView (static_cast<View> (index)); };
        addAndMakeVisible (tabs);

        theme.onChange = [this] (int index)
        {
            auto settings = processor.getMachineSettings();
            settings.darkTheme = index == 1;
            processor.setMachineSettings (settings);
            editor.look.setDark (settings.darkTheme);
            editor.sendLookAndFeelChange();
            editor.repaint();
        };
        addAndMakeVisible (theme);

        gear.onClick = [this] { showSettingsMenu(); };
        addAndMakeVisible (gear);

        perspectiveAttachment = std::make_unique<juce::ParameterAttachment> (
            *processor.getState().getParameter (SoundParameters::Ids::perspective),
            [this] (float value) { perspective.setSelected (value >= 0.5f ? 1 : 0); });
        perspective.setTooltips ({ "A stereo pair over the open lid", "Where the pianist sits" });
        perspective.onChange = [this] (int index) { perspectiveAttachment->setValueAsCompleteGesture (static_cast<float> (index)); };
        perspectiveAttachment->sendInitialUpdate();
        addAndMakeVisible (perspective);
        addAndMakeVisible (presetNav);

        about.setVisible (false);
        addChildComponent (about);
    }

    void show (View newView)
    {
        current = newView;
        tabs.setSelected (static_cast<int> (newView));
        theme.setSelected (editor.look.isDark() ? 1 : 0);

        simple.reset();
        advanced.reset();
        lab.reset();

        juce::Component* view = nullptr;
        switch (newView)
        {
            case View::simple:   simple = std::make_unique<SimpleView> (processor); view = simple.get(); break;
            case View::advanced: advanced = std::make_unique<AdvancedView> (processor); view = advanced.get(); break;
            case View::lab:      lab = std::make_unique<LabView> (processor); view = lab.get(); break;
        }

        view->setBounds (0, 0, baseWidth, baseHeight);
        addAndMakeVisible (*view, 0);  // under the header
        about.toFront (false);
        resized();
        repaint();
    }

    void tick()
    {
        presetNav.tick();
        if (simple != nullptr)
            simple->tick();
        if (advanced != nullptr)
            advanced->tick();

        if (current != View::simple)
        {
            const int notes = processor.getEngine().getNumSoundingNotes();
            const int cpu = juce::roundToInt (100.0f * processor.getCpuLoad());
            if (notes != shownNotes || cpu != shownCpu)
            {
                shownNotes = notes;
                shownCpu = cpu;
                repaint (meterArea());
            }
        }
    }

    void paint (juce::Graphics& g) override
    {
        const auto& t = editor.look.theme();
        g.setGradientFill (juce::ColourGradient::vertical (t.bg, 0.0f, t.bg2, static_cast<float> (baseHeight)));
        g.fillAll();

        if (current == View::simple)
            return;

        g.setColour (t.ink);
        g.setFont (ForteLookAndFeel::italiana (28.0f, 0.28f));
        g.drawText ("FORTE", 24, 12, 140, 34, juce::Justification::centredLeft);

        g.setColour (t.muted);
        g.setFont (ForteLookAndFeel::jost (13.0f));
        g.drawText ("Notes " + juce::String (shownNotes) + "   CPU " + juce::String (shownCpu) + "%", meterArea(),
                    juce::Justification::centredRight);

        g.setColour (t.line);
        g.drawHorizontalLine (58, 24.0f, static_cast<float> (baseWidth - 24));
    }

    void resized() override
    {
        if (current == View::simple)
        {
            tabs.setBounds (32, 26, 258, 28);
            presetNav.setBounds (430, 104, 240, 26);  // under the wordmark
            perspective.setBounds (448, 140, 184, 26);
            theme.setBounds (938, 27, 92, 26);
        }
        else
        {
            tabs.setBounds (168, 16, 258, 26);
            perspective.setBounds (444, 16, 184, 26);
            presetNav.setBounds (640, 16, 180, 26);
            theme.setBounds (938, 16, 92, 26);
        }

        gear.setBounds (1040, current == View::simple ? 25 : 14, 30, 30);
        about.setBounds (getLocalBounds());
    }

private:
    [[nodiscard]] static juce::Rectangle<int> meterArea() { return { 822, 16, 106, 26 }; }

    void showSettingsMenu()
    {
        const auto settings = processor.getMachineSettings();
        juce::PopupMenu menu;
        menu.addItem (1, "Invert sustain pedal", true, settings.invertSustain);
        menu.addItem (2, "Invert soft pedal", true, settings.invertSoft);
        menu.addSeparator();
        menu.addItem (3, "About Forte");

        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&gear),
                            [safe = juce::Component::SafePointer<Content> (this)] (int choice)
                            {
                                if (safe == nullptr || choice == 0)
                                    return;

                                auto changed = safe->processor.getMachineSettings();
                                if (choice == 1)
                                    changed.invertSustain = ! changed.invertSustain;
                                else if (choice == 2)
                                    changed.invertSoft = ! changed.invertSoft;
                                else
                                {
                                    safe->about.setVisible (true);
                                    safe->about.toFront (false);
                                    return;
                                }
                                safe->processor.setMachineSettings (changed);
                            });
    }

    PluginProcessor& processor;
    PluginEditor& editor;
    View current = View::simple;

    Segmented tabs { { "Simple", "Advanced", "Lab" } };
    Segmented theme { { "Light", "Dark" } };
    Segmented perspective { { "Microphones", "Pianist" } };
    std::unique_ptr<juce::ParameterAttachment> perspectiveAttachment;
    GearButton gear;
    PresetNav presetNav { processor.getPresets() };
    AboutPanel about;

    std::unique_ptr<SimpleView> simple;
    std::unique_ptr<AdvancedView> advanced;
    std::unique_ptr<LabView> lab;

    int shownNotes = 0, shownCpu = 0;
};

//==============================================================================
PluginEditor::PluginEditor (PluginProcessor& p)
    : juce::AudioProcessorEditor (p), processor (p)
{
    look.setDark (processor.getMachineSettings().darkTheme);
    setLookAndFeel (&look);

    content = std::make_unique<Content> (processor, *this);
    addAndMakeVisible (*content);
    shown = processor.view.load();
    content->show (shown);

    tooltips = std::make_unique<juce::TooltipWindow> (this, 700);

    setResizable (true, true);
    getConstrainer()->setFixedAspectRatio (static_cast<double> (baseWidth) / baseHeight);
    setResizeLimits (baseWidth * 6 / 10, baseHeight * 6 / 10, baseWidth * 2, baseHeight * 2);
    setSize (baseWidth, baseHeight);
    startTimerHz (30);
}

PluginEditor::~PluginEditor()
{
    stopTimer();
    tooltips.reset();
    content.reset();
    setLookAndFeel (nullptr);
}

void PluginEditor::showView (View view)
{
    processor.view.store (view);
    shown = view;
    // After the click that asked for it, not inside it: the click's button is in the old view.
    juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<PluginEditor> (this), view]
    {
        if (safe != nullptr && safe->content != nullptr)
            safe->content->show (view);
    });
}

void PluginEditor::paint (juce::Graphics& g)
{
    g.fillAll (look.theme().bg);
}

void PluginEditor::resized()
{
    if (content != nullptr)
    {
        content->setBounds (0, 0, baseWidth, baseHeight);
        content->setTransform (juce::AffineTransform::scale (static_cast<float> (getWidth()) / baseWidth));
    }
}

void PluginEditor::timerCallback()
{
    // A project loaded while the editor is open brings its own view.
    if (const auto wanted = processor.view.load(); wanted != shown)
        showView (wanted);

    content->tick();
}
} // namespace forte
