#include "PluginProcessor.h"
#include "PluginEditor.h"

MixAgentAudioProcessorEditor::MixAgentAudioProcessorEditor(MixAgentAudioProcessor& p)
    : AudioProcessorEditor(p), proc(p)
{
    setResizable(true, true);
    setResizeLimits(1080, 720, 1600, 1000);
    setSize(1160, 760);

    logoLabel.setText("", juce::dontSendNotification);
    logoLabel.setFont(juce::Font(juce::FontOptions(24.0f, juce::Font::bold)));
    logoLabel.setColour(juce::Label::textColourId, agm::ui::kText);
    logoLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(logoLabel);

    subLabel.setText("ALL-IN-ONE MIXING STRIP", juce::dontSendNotification);
    subLabel.setFont(juce::Font(juce::FontOptions(10.0f, juce::Font::bold)));
    subLabel.setColour(juce::Label::textColourId, agm::ui::kAccent);
    subLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(subLabel);

    for (int i = 0; i < proc.getNumPrograms(); ++i)
        presetCombo.addItem(proc.getProgramName(i), i + 1);
    presetCombo.setSelectedId(proc.getCurrentProgram() + 1, juce::dontSendNotification);
    presetCombo.setTooltip("Factory presets replace the complete strip. Init restores defaults.");
    presetCombo.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff0a0a0d));
    presetCombo.setColour(juce::ComboBox::textColourId, agm::ui::kAccentHot);
    presetCombo.setColour(juce::ComboBox::arrowColourId, agm::ui::kTextDim);
    presetCombo.setColour(juce::ComboBox::outlineColourId, agm::ui::kBorder);
    presetCombo.onChange = [this] { proc.setCurrentProgram(presetCombo.getSelectedItemIndex()); };
    addAndMakeVisible(presetCombo);

    auto smallLabel = [](juce::Label& l)
    {
        l.setFont(juce::Font(juce::FontOptions(9.0f, juce::Font::bold)));
        l.setColour(juce::Label::textColourId, agm::ui::kTextDim);
        l.setJustificationType(juce::Justification::centred);
    };
    smallLabel(inLabel);
    smallLabel(outLabel);
    addAndMakeVisible(inLabel);
    addAndMakeVisible(outLabel);
    addAndMakeVisible(inMeter);
    addAndMakeVisible(outMeter);
    addAndMakeVisible(spectrum);
    for (auto* parameter : proc.getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(parameter))
            if (ranged->paramID.startsWith("eq_"))
                eqDisplayParameters.push_back({proc.getAPVTS().getRawParameterValue(ranged->paramID),
                                                std::numeric_limits<float>::quiet_NaN()});
    addAndMakeVisible(compMeter);
    addAndMakeVisible(limMeter);
    inTrim = addKnob("in_gain", "INPUT", " dB", 1);
    outTrim = addKnob("out_gain", "OUTPUT", " dB", 1);

    satModeCombo.addItemList({ "Tube", "Tape", "Soft", "Exciter" }, 1);
    satModeCombo.setSelectedId(1);
    satModeCombo.setColour(juce::ComboBox::backgroundColourId, agm::ui::kPanelHi);
    satModeCombo.setColour(juce::ComboBox::textColourId, agm::ui::kText);
    satModeCombo.setColour(juce::ComboBox::arrowColourId, agm::ui::kTextDim);
    satModeCombo.setColour(juce::ComboBox::outlineColourId, agm::ui::kBorder);
    satModeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        proc.getAPVTS(), "sat_mode", satModeCombo);
    addAndMakeVisible(satModeCombo);

    addPower("eq_enabled");
    addPower("sat_enabled");
    addPower("comp_enabled");
    addPower("img_enabled");
    addPower("dly_enabled");
    addPower("rvb_enabled");
    addPower("lim_enabled");
    addPower("inst_enabled");

    hpToggle = addToggle("eq_hp_enabled", "HP");
    lpToggle = addToggle("eq_lp_enabled", "LP");
    monoToggle = addToggle("img_mono", "MONO");

    auto hzText = [](double v)
    {
        return v >= 1000.0 ? juce::String(v / 1000.0, 1) + " kHz"
                           : juce::String(juce::roundToInt(v)) + " Hz";
    };
    auto pctText = [](double v) { return juce::String(juce::roundToInt(v * 100.0)) + "%"; };

    hpKnob = addKnob("eq_hp_freq", "HP", "", 0);
    hpKnob->textFromValueFunction = hzText;
    lpKnob = addKnob("eq_lp_freq", "LP", "", 0);
    lpKnob->textFromValueFunction = hzText;
    lsfF = addKnob("eq_lsf_freq", "LS F", "", 0);
    lsfF->textFromValueFunction = hzText;
    lsfG = addKnob("eq_lsf_gain", "LS G", " dB", 1);
    hsfF = addKnob("eq_hsf_freq", "HS F", "", 0);
    hsfF->textFromValueFunction = hzText;
    hsfG = addKnob("eq_hsf_gain", "HS G", " dB", 1);
    p1F = addKnob("eq_p1_freq", "P1 F", "", 0);
    p1F->textFromValueFunction = hzText;
    p1G = addKnob("eq_p1_gain", "P1 G", " dB", 1);
    p1Q = addKnob("eq_p1_q", "P1 Q", "", 2);
    p2F = addKnob("eq_p2_freq", "P2 F", "", 0);
    p2F->textFromValueFunction = hzText;
    p2G = addKnob("eq_p2_gain", "P2 G", " dB", 1);
    p2Q = addKnob("eq_p2_q", "P2 Q", "", 2);
    p3F = addKnob("eq_p3_freq", "P3 F", "", 0);
    p3F->textFromValueFunction = hzText;
    p3G = addKnob("eq_p3_gain", "P3 G", " dB", 1);
    p3Q = addKnob("eq_p3_q", "P3 Q", "", 2);

    satDrive = addKnob("sat_drive", "DRIVE", "", 0);
    satDrive->textFromValueFunction = pctText;
    satMix = addKnob("sat_mix", "MIX", "", 0);
    satMix->textFromValueFunction = pctText;
    satOut = addKnob("sat_out", "OUT", " dB", 1);

    compT = addKnob("comp_thresh", "THRESH", " dB", 0);
    compR = addKnob("comp_ratio", "RATIO", ":1", 1);
    compA = addKnob("comp_attack", "ATTACK", " ms", 0);
    compRel = addKnob("comp_release", "RELEASE", " ms", 0);
    compK = addKnob("comp_knee", "KNEE", " dB", 0);
    compMix = addKnob("comp_mix", "MIX", "", 0);
    compMix->textFromValueFunction = pctText;
    compMake = addKnob("comp_makeup", "MAKEUP", " dB", 0);

    imgW = addKnob("img_width", "WIDTH", "", 0);
    imgW->textFromValueFunction = [](double v) { return juce::String(juce::roundToInt(v)) + "%"; };
    imgB = addKnob("img_balance", "BAL", "", 2);

    dlyT = addKnob("dly_time", "TIME", " ms", 0);
    dlyF = addKnob("dly_feedback", "FEEDBACK", "", 0);
    dlyF->textFromValueFunction = pctText;
    dlyM = addKnob("dly_mix", "MIX", "", 0);
    dlyM->textFromValueFunction = pctText;
    dlyD = addKnob("dly_damp", "DAMP", "", 0);
    dlyD->textFromValueFunction = pctText;
    dlyW = addKnob("dly_width", "WIDTH", "", 0);
    dlyW->textFromValueFunction = pctText;

    rvbS = addKnob("rvb_size", "SIZE", "", 2);
    rvbDc = addKnob("rvb_decay", "DECAY", " s", 1);
    rvbD = addKnob("rvb_damp", "DAMP", "", 0);
    rvbD->textFromValueFunction = pctText;
    rvbW = addKnob("rvb_width", "WIDTH", "", 0);
    rvbW->textFromValueFunction = pctText;
    rvbM = addKnob("rvb_mix", "MIX", "", 0);
    rvbM->textFromValueFunction = pctText;
    rvbP = addKnob("rvb_predelay", "PRE-DELAY", " ms", 0);

    limC = addKnob("lim_ceiling", "CEILING", " dB", 1);
    limA = addKnob("lim_attack", "ATTACK", " ms", 1);
    limR = addKnob("lim_release", "RELEASE", " ms", 0);

    padLabel.setFont(juce::Font(juce::FontOptions(9.0f, juce::Font::bold)));
    padLabel.setColour(juce::Label::textColourId, agm::ui::kTextDim);
    padLabel.setJustificationType(juce::Justification::centredRight);
    padLabel.setText("MELODY: CH 1-9 / 11-16   DRUMS: CH 10", juce::dontSendNotification);
    addAndMakeVisible(padLabel);

     for (int i = 0; i < (int)agm::InstrumentBank::kCount; ++i)
        instProgramCombo.addItem(agm::InstrumentBank::programName(i), i + 1);
    instProgramCombo.setSelectedId(1);
    instProgramCombo.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff0a0a0d));
    instProgramCombo.setColour(juce::ComboBox::textColourId, agm::ui::kAccentHot);
    instProgramCombo.setColour(juce::ComboBox::arrowColourId, agm::ui::kTextDim);
    instProgramCombo.setColour(juce::ComboBox::outlineColourId, agm::ui::kBorder);
    // Filtered lists keep stable program IDs. ComboBoxAttachment uses the
    // visible item index, which would play a different sound in Favorites.
    instProgramAttachment = std::make_unique<juce::ParameterAttachment>(
        *proc.getAPVTS().getParameter("inst_program"), [this](float value)
        {
            const int program = juce::roundToInt(value);
            if (instProgramCombo.indexOfItemId(program + 1) < 0)
                instProgramCombo.addItem(juce::String(agm::InstrumentBank::programName(program)) + " (selected)", program + 1);
            instProgramCombo.setSelectedId(program + 1, juce::dontSendNotification);
            favToggle.setToggleState(proc.isFavorite(program), juce::dontSendNotification);
        }, nullptr);
    instProgramAttachment->sendInitialUpdate();
    instProgramCombo.onChange = [this]
    {
        const int item = instProgramCombo.getSelectedId();
        if (item >= 1 && item <= (int)agm::InstrumentBank::kCount)
        {
            instProgramAttachment->setValueAsCompleteGesture((float)(item - 1));
            favToggle.setToggleState(proc.isFavorite(item - 1), juce::dontSendNotification);
        }
    };
    addAndMakeVisible(instProgramCombo);
    auditionButton.setTooltip("Preview the selected instrument for 300 ms. Preset changes never inject notes into playback.");
    auditionButton.onClick = [this]
    {
        if (auditionTimerRemaining > 0) proc.uiNoteOff(auditionNote);
        auditionNote = keyboardBaseNote + 12;
        proc.uiNoteOn(auditionNote, 0.85f);
        auditionTimerRemaining = 6; // 20 Hz timer: 300 ms
    };
    addAndMakeVisible(auditionButton);
    auditionButton.setComponentID("audition");
    stopNotesButton.setComponentID("stop-notes");
    stopNotesButton.setTooltip("Stop all held preview and incoming MIDI voices on the next audio callback.");
    stopNotesButton.onClick = [this]
    {
        padGrid.releaseAll(); auditionTimerRemaining = 0; proc.uiAllSoundOff();
    };
    addAndMakeVisible(stopNotesButton);
    octaveDown.setComponentID("octave-down"); octaveUp.setComponentID("octave-up");
    octaveDown.setTooltip("Move the preview keyboard down one octave.");
    octaveUp.setTooltip("Move the preview keyboard up one octave.");
    octaveDown.onClick = [this] { setKeyboardOctave(keyboardBaseNote - 12); };
    octaveUp.onClick = [this] { setKeyboardOctave(keyboardBaseNote + 12); };
    addAndMakeVisible(octaveDown); addAndMakeVisible(octaveUp); addAndMakeVisible(octaveLabel);
    octaveLabel.setJustificationType(juce::Justification::centred);
    octaveLabel.setColour(juce::Label::textColourId, agm::ui::kCyan);
    octaveLabel.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)));

    instFilterCombo.addItem("ALL", 1);
    instFilterCombo.addItem("FAVORITES", 2);
    instFilterCombo.addItem("KEYS/LEAD", 3);
    instFilterCombo.addItem("BASS", 4);
    instFilterCombo.setSelectedId(1);
    instFilterCombo.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff0a0a0d));
    instFilterCombo.setColour(juce::ComboBox::textColourId, agm::ui::kAccentHot);
    instFilterCombo.setColour(juce::ComboBox::arrowColourId, agm::ui::kTextDim);
    instFilterCombo.setColour(juce::ComboBox::outlineColourId, agm::ui::kBorder);
    instFilterCombo.onChange = [this] { updateProgramList(); };
    addAndMakeVisible(instFilterCombo);

    favToggle.setColour(juce::ToggleButton::textColourId, agm::ui::kAccent);
    favToggle.onClick = [this] { onClickFav(); };
    addAndMakeVisible(favToggle);

    instLevel = addKnob("inst_level", "LEVEL", " dB", 1);

    // chromatic preview keyboard (one octave, triggers instruments via UI queue)
    setKeyboardOctave(48);
    padGrid.setComponentID("preview-keys");
    padGrid.setTooltip("Hold a key to play; drag across keys to glide. Use the octave buttons to explore the instrument.");
    addAndMakeVisible(padGrid);

    startTimerHz(20);
    fullyBuilt = true;
    resized();
}

agm::ui::Knob* MixAgentAudioProcessorEditor::addKnob(const juce::String& id, const juce::String& label,
                                                     const juce::String& suffix, int decimals)
{
    auto* knob = knobs.add(new agm::ui::Knob(label, suffix));
    knob->setNumDecimalPlacesToDisplay(decimals);
    if (auto* p = dynamic_cast<juce::RangedAudioParameter*>(proc.getAPVTS().getParameter(id)))
    {
        knob->setRange(p->getNormalisableRange().start, p->getNormalisableRange().end, p->getNormalisableRange().interval);
        knob->setDoubleClickReturnValue(true, p->convertFrom0to1(p->getDefaultValue()));
        knob->setComponentID(id);
        using Unit = agm::ui::Knob::EntryUnit;
        if (id.contains("freq")) knob->setEntryUnit(Unit::Frequency);
        else if (suffix == " dB") knob->setEntryUnit(Unit::Decibels);
        else if (suffix == " ms") knob->setEntryUnit(Unit::Milliseconds);
        else if (suffix == " s") knob->setEntryUnit(Unit::Seconds);
        else if (suffix == ":1") knob->setEntryUnit(Unit::Ratio);
        else if (id == "img_width" || (p->getNormalisableRange().start == 0.0f
                                       && p->getNormalisableRange().end == 1.0f)) knob->setEntryUnit(Unit::Percent);
        knob->setTooltip(p->getName(128) + ": drag to adjust; Shift-drag for fine control; click the value or right-click to type; double-click the dial to reset.");
        knob->onValueEntered = [knob, p](double value)
        {
            p->beginChangeGesture(); knob->setValue(value, juce::sendNotificationSync); p->endChangeGesture();
        };
        knobAttachments.add(new juce::AudioProcessorValueTreeState::SliderAttachment(proc.getAPVTS(), id, *knob));
    }
    addAndMakeVisible(knob);
    return knob;
}

void MixAgentAudioProcessorEditor::setKeyboardOctave(int note)
{
    keyboardBaseNote = juce::jlimit(24, 84, note);
    if (auditionTimerRemaining > 0) proc.uiNoteOff(auditionNote);
    auditionTimerRemaining = 0;
    static const char* names[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    std::vector<agm::ui::PadGrid::Pad> keys;
    for (int i = 0; i < 12; ++i)
    {
        const bool sharp = i == 1 || i == 3 || i == 6 || i == 8 || i == 10;
        keys.push_back({names[i], keyboardBaseNote + i, juce::Colour(sharp ? 0xff42628f : 0xff2f7dff)});
    }
    padGrid.setPads(std::move(keys));
    octaveLabel.setText("C" + juce::String(keyboardBaseNote / 12 - 1), juce::dontSendNotification);
    octaveDown.setEnabled(keyboardBaseNote > 24); octaveUp.setEnabled(keyboardBaseNote < 84);
}

juce::ToggleButton* MixAgentAudioProcessorEditor::addPower(const juce::String& id)
{
    auto* t = powerToggles.add(new agm::ui::PowerToggle());
    if (auto* parameter = proc.getAPVTS().getParameter(id))
    {
        t->setTitle(parameter->getName(128));
        t->setTooltip(parameter->getName(128) + ": enable or bypass this module.");
    }
    t->setComponentID(id);
    buttonAttachments.add(new juce::AudioProcessorValueTreeState::ButtonAttachment(proc.getAPVTS(), id, *t));
    addAndMakeVisible(t);
    return t;
}

juce::ToggleButton* MixAgentAudioProcessorEditor::addToggle(const juce::String& id, const juce::String& text)
{
    auto* t = utilityToggles.add(new agm::ui::PowerToggle(text));
    buttonAttachments.add(new juce::AudioProcessorValueTreeState::ButtonAttachment(proc.getAPVTS(), id, *t));
    addAndMakeVisible(t);
    return t;
}

void MixAgentAudioProcessorEditor::updateProgramList()
{
    const int filter = instFilterCombo.getSelectedId();
    const int currentId = proc.getInstrumentProgram() + 1;
    instProgramCombo.clear(juce::dontSendNotification);

    for (int i = 0; i < (int)agm::InstrumentBank::kCount; ++i)
    {
        const juce::String cat = agm::InstrumentBank::programCategory(i);
        const bool matchesFav = (filter != 2) || proc.isFavorite(i);
        const bool matchesCat = (filter != 3 || cat == "Keys/Lead") && (filter != 4 || cat == "Bass");
        if (matchesFav && matchesCat)
            instProgramCombo.addItem(agm::InstrumentBank::programName(i), i + 1);
    }
    if (instProgramCombo.indexOfItemId(currentId) < 0)
        instProgramCombo.addItem(juce::String(agm::InstrumentBank::programName(currentId - 1)) + " (selected)", currentId);
    instProgramCombo.setSelectedId(currentId, juce::dontSendNotification);

    const int item = instProgramCombo.getSelectedId();
    if (item >= 1 && item <= (int)agm::InstrumentBank::kCount)
        favToggle.setToggleState(proc.isFavorite(item - 1), juce::dontSendNotification);
}

void MixAgentAudioProcessorEditor::onClickFav()
{
    const int item = instProgramCombo.getSelectedId();
    if (item < 1 || item > (int)agm::InstrumentBank::kCount)
        return;
    const bool nowFav = !proc.isFavorite(item - 1);
    proc.setFavorite(item - 1, nowFav);
    updateProgramList();
}

void MixAgentAudioProcessorEditor::placeKnobs(float centreX, int y, const juce::Array<agm::ui::Knob*>& ks, int w, int h)
{
    const int gap = 4;
    const int totalW = ks.size() * w + ((int)ks.size() - 1) * gap;
    int x = juce::roundToInt(centreX) - totalW / 2;
    for (auto* k : ks)
    {
        k->setBounds(x, y, w, h);
        x += w + gap;
    }
}

void MixAgentAudioProcessorEditor::drawHeader(juce::Graphics& g, juce::Rectangle<int> r, const char* name)
{
    g.setColour(agm::ui::kTextDim);
    g.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)));
    g.drawText(name, r.getX() + 10, r.getY() + 3, r.getWidth() - 20, 20, juce::Justification::centredLeft);
    const float w = juce::GlyphArrangement::getStringWidth(g.getCurrentFont(), name);
    const float ux = (float)(r.getX() + 10);
    const float uy = (float)(r.getY() + 24);
    g.setGradientFill(juce::ColourGradient(agm::ui::kAccent, ux, uy,
                                           agm::ui::kAccent.withAlpha(0.0f), ux + w + 34.0f, uy, false));
    g.fillRect(ux, uy, w + 34.0f, 2.0f);
}

void MixAgentAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.setGradientFill(agm::ui::verticalFade(getLocalBounds().toFloat(),
                                            juce::Colour(0xff0c0c10), juce::Colour(0xff131318)));
    g.fillAll();

    auto panelPaint = [&](juce::Rectangle<int> r)
    {
        agm::ui::panelBevel(g, r.toFloat(), 6.0f);
        g.setColour(agm::ui::kBorder.withAlpha(0.6f));
        g.drawHorizontalLine(r.getY() + 27, (float)r.getX() + 8.0f, (float)r.getRight() - 8.0f);
    };

    panelPaint(topBarRect);
    panelPaint(eqSection);
    for (auto& p : panels)
        panelPaint(p);

    {
        const juce::String logo = "MIXAGENT";
        g.setFont(juce::Font(juce::FontOptions(24.0f, juce::Font::bold)));
        const auto lr = topBarRect.reduced(14, 0).withWidth(220).withY(topBarRect.getY() + 4).withHeight(36);
        g.setColour(juce::Colours::black.withAlpha(0.85f));
        g.drawText(logo, lr.translated(1, 1), juce::Justification::centredLeft, false);
        g.setColour(juce::Colour(0xff3a3a44).withAlpha(0.6f));
        g.drawText(logo, lr.translated(-1, -1), juce::Justification::centredLeft, false);
        g.setColour(agm::ui::kText);
        g.drawText(logo, lr, juce::Justification::centredLeft, false);
    }

    {
        auto dr = drumRect.toFloat();
        const float corner = 6.0f;
        g.setGradientFill(agm::ui::verticalFade(dr, agm::ui::kPanelHi, agm::ui::kPanel));
        g.fillRoundedRectangle(dr, corner);
        g.setColour(agm::ui::kAccent.withAlpha(0.06f));
        g.fillRoundedRectangle(dr, corner);
        g.setGradientFill(juce::ColourGradient(agm::ui::kCyan.withAlpha(0.07f), dr.getX(), dr.getY(),
                                               agm::ui::kCyan.withAlpha(0.0f), dr.getX(), dr.getY() + 70.0f, false));
        g.fillRoundedRectangle(dr, corner);
        g.setColour(agm::ui::kBorder);
        g.drawRoundedRectangle(dr.reduced(0.5f), corner, 1.0f);
        g.setColour(juce::Colours::white.withAlpha(0.22f));
        g.fillRect(dr.getX() + corner, dr.getY() + 0.5f,
                   juce::jmax(0.0f, dr.getWidth() - corner * 2.0f), 1.0f);
        g.setColour(agm::ui::kCyan);
        g.fillRect(dr.getX() + corner, dr.getY() + 1.5f,
                   juce::jmax(0.0f, dr.getWidth() - corner * 2.0f), 1.0f);
    }

    drawHeader(g, eqSection, "EQUALIZER");
    drawHeader(g, panels[0], "SATURATION");
    drawHeader(g, panels[1], "COMPRESSOR");
    drawHeader(g, panels[2], "STEREO IMAGER");
    drawHeader(g, panels[3], "DELAY");
    drawHeader(g, panels[4], "REVERB");
    drawHeader(g, panels[5], "LIMITER");

    {
        const char* name = "INSTRUMENTS";
        g.setColour(agm::ui::kAccentHot);
        g.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)));
        g.drawText(name, drumRect.getX() + 34, drumRect.getY() + 5, 112, 20,
                   juce::Justification::centredLeft);
        const float w = juce::GlyphArrangement::getStringWidth(g.getCurrentFont(), name);
        const float ux = (float)(drumRect.getX() + 34);
        const float uy = (float)(drumRect.getY() + 24);
        g.setGradientFill(juce::ColourGradient(agm::ui::kAccent, ux, uy,
                                               agm::ui::kAccent.withAlpha(0.0f), ux + w + 34.0f, uy, false));
        g.fillRect(ux, uy, w + 34.0f, 2.0f);
    }

    const auto combo = satModeCombo.getBounds();
    if (combo.getHeight() > 0)
    {
        g.setColour(agm::ui::kTextDim);
        g.setFont(juce::Font(juce::FontOptions(8.5f, juce::Font::bold)));
        g.drawText("MODE", combo.withHeight(12).withY(combo.getY() - 14),
                   juce::Justification::centred, false);
    }
}

void MixAgentAudioProcessorEditor::resized()
{
    if (!fullyBuilt) return;
    auto available = getLocalBounds().reduced(agm::ui::kMargin);
    topBarRect = available.removeFromTop(64);
    available.removeFromTop(10);
    eqSection = available.removeFromTop(juce::jlimit(194, 220, juce::roundToInt(getHeight() * 0.28f)));
    available.removeFromTop(10);
    drumRect = available.removeFromBottom(130);
    available.removeFromBottom(10);
    modulesRow = available;

    auto top = topBarRect.reduced(14, 0);
    logoLabel.setBounds(top.removeFromLeft(210).withY(topBarRect.getY() + 4).withHeight(36));
    subLabel.setBounds(top.removeFromLeft(190).withY(topBarRect.getY() + 20).withHeight(14));
    presetCombo.setBounds(top.removeFromRight(170).withSizeKeepingCentre(170, 26));
    top.removeFromRight(16);
    outLabel.setBounds(top.removeFromRight(30).withSizeKeepingCentre(30, 12));
    outMeter.setBounds(top.removeFromRight(14).withSizeKeepingCentre(14, 36));
    outTrim->setBounds(top.removeFromRight(52).withSizeKeepingCentre(52, 60));
    top.removeFromRight(10);
    inLabel.setBounds(top.removeFromRight(30).withSizeKeepingCentre(30, 12));
    inMeter.setBounds(top.removeFromRight(14).withSizeKeepingCentre(14, 36));
    inTrim->setBounds(top.removeFromRight(52).withSizeKeepingCentre(52, 60));

    powerToggles[0]->setBounds(eqSection.getRight() - 30, eqSection.getY() + 7, 18, 16);
    const int knobY = eqSection.getBottom() - 80;
    spectrum.setBounds(eqSection.getX() + 10, eqSection.getY() + 32,
                       eqSection.getWidth() - 20, knobY - eqSection.getY() - 38);
    const float groupWidth = (float)eqSection.getWidth() / 7.0f;
    auto centre = [&](int index) { return eqSection.getX() + (index + 0.5f) * groupWidth; };
    const int eqKnobWidth = juce::jmin(50, (int)(groupWidth - 8.0f) / 3);
    hpKnob->setBounds((int)centre(0) - 2, knobY, 50, 72);
    hpToggle->setBounds((int)centre(0) - 56, knobY + 27, 48, 18);
    lpKnob->setBounds((int)centre(6) - 2, knobY, 50, 72);
    lpToggle->setBounds((int)centre(6) - 56, knobY + 27, 48, 18);
    placeKnobs(centre(1), knobY, {lsfF, lsfG}, eqKnobWidth);
    placeKnobs(centre(2), knobY, {p1F, p1G, p1Q}, eqKnobWidth);
    placeKnobs(centre(3), knobY, {p2F, p2G, p2Q}, eqKnobWidth);
    placeKnobs(centre(4), knobY, {p3F, p3G, p3Q}, eqKnobWidth);
    placeKnobs(centre(5), knobY, {hsfF, hsfG}, eqKnobWidth);

    constexpr int gap = 10;
    constexpr int weights[] = {186, 210, 186, 186, 186, 140};
    constexpr int totalWeight = 1094;
    const int usableWidth = modulesRow.getWidth() - gap * 5;
    int x = modulesRow.getX();
    for (int i = 0; i < 6; ++i)
    {
        const int width = i == 5 ? modulesRow.getRight() - x : usableWidth * weights[i] / totalWeight;
        panels[i] = {x, modulesRow.getY(), width, modulesRow.getHeight()};
        powerToggles[i + 1]->setBounds(panels[i].getRight() - 26, panels[i].getY() + 7, 18, 16);
        x += width + gap;
    }
    constexpr int kh = 72;
    const int contentY = modulesRow.getY() + 34;
    const int contentH = modulesRow.getHeight() - 42;
    const int rowGap = juce::jlimit(3, 24, (contentH - kh * 3) / 2);
    const int row1 = contentY + juce::jmax(0, (contentH - kh * 3 - rowGap * 2) / 2);
    const int row2 = row1 + kh + rowGap, row3 = row2 + kh + rowGap;
    const int satY = contentY + (contentH - 224) / 2;
    placeKnobs(panels[0].getCentreX(), satY, {satDrive}, 84, 96);
    placeKnobs(panels[0].getCentreX(), satY + 100, {satMix, satOut}, 54, kh);
    satModeCombo.setBounds(panels[0].getX() + 20, satY + 198, panels[0].getWidth() - 40, 26);

    auto compArea = panels[1].withTrimmedRight(28);
    const int compWidth = juce::jmin(52, (compArea.getWidth() - 16) / 3);
    compMeter.setBounds(panels[1].getRight() - 24, contentY, 16, contentH);
    placeKnobs(compArea.getCentreX(), row1, {compT, compR, compA}, compWidth, kh);
    placeKnobs(compArea.getCentreX(), row2, {compRel, compK, compMix}, compWidth, kh);
    placeKnobs(compArea.getCentreX(), row3, {compMake}, compWidth, kh);

    const int imageY = contentY + (contentH - 124) / 2;
    placeKnobs(panels[2].getCentreX(), imageY, {imgW, imgB}, 72, 88);
    monoToggle->setBounds(panels[2].getCentreX() - 32, imageY + 100, 64, 20);
    placeKnobs(panels[3].getCentreX(), row1, {dlyT, dlyF}, 54, kh);
    placeKnobs(panels[3].getCentreX(), row2, {dlyM, dlyD}, 54, kh);
    placeKnobs(panels[3].getCentreX(), row3, {dlyW}, 54, kh);
    placeKnobs(panels[4].getCentreX(), row1, {rvbS, rvbDc}, 54, kh);
    placeKnobs(panels[4].getCentreX(), row2, {rvbD, rvbW}, 54, kh);
    placeKnobs(panels[4].getCentreX(), row3, {rvbM, rvbP}, 54, kh);
    auto limArea = panels[5].withTrimmedRight(28);
    limMeter.setBounds(panels[5].getRight() - 24, contentY, 16, contentH);
    placeKnobs(limArea.getCentreX(), row1, {limC}, 64, 84);
    const int limiterWidth = juce::jmin(48, (limArea.getWidth() - 12) / 2);
    placeKnobs(limArea.getCentreX(), row2 + 12, {limA, limR}, limiterWidth, kh);

    const int libraryX = drumRect.getX(), libraryY = drumRect.getY();
    powerToggles[7]->setBounds(libraryX + 10, libraryY + 7, 18, 16);
    instProgramCombo.setBounds(libraryX + 150, libraryY + 5, 156, 24);
    instFilterCombo.setBounds(libraryX + 314, libraryY + 5, 108, 24);
    favToggle.setBounds(libraryX + 430, libraryY + 4, 58, 26);
    auditionButton.setBounds(libraryX + 496, libraryY + 5, 86, 24);
    stopNotesButton.setBounds(libraryX + 590, libraryY + 5, 86, 24);
    octaveDown.setBounds(libraryX + 686, libraryY + 5, 24, 24);
    octaveLabel.setBounds(libraryX + 714, libraryY + 5, 44, 24);
    octaveUp.setBounds(libraryX + 762, libraryY + 5, 24, 24);
    padLabel.setBounds(libraryX + 800, libraryY + 7, drumRect.getWidth() - 810, 20);
    instLevel->setBounds(drumRect.getRight() - 64, libraryY + 46, 54, 72);
    padGrid.setBounds(drumRect.withTrimmedTop(44).withTrimmedRight(74).reduced(8, 6));
}

void MixAgentAudioProcessorEditor::timerCallback()
{
    presetCombo.setSelectedId(proc.getCurrentProgram() + 1, juce::dontSendNotification);
    inMeter.setLevel(proc.getInLevel(0), proc.getInLevel(1));
    outMeter.setLevel(proc.getOutLevel(0), proc.getOutLevel(1));
    compMeter.setGrDb(proc.getCompGrDb());
    limMeter.setGrDb(proc.getLimGrDb());

    float spec[600];
    float curve[600];
    proc.getAnalyzerSpectrum(spec, 600);
    const double processingRate = proc.getProcessingSampleRate();
    bool eqChanged = eqDisplayRate != processingRate;
    eqDisplayRate = processingRate;
    for (auto& parameter : eqDisplayParameters)
    {
        const float value = parameter.first->load(std::memory_order_relaxed);
        eqChanged |= value != parameter.second; parameter.second = value;
    }
    spectrum.setSpectrum(spec, 600);
    if (eqChanged)
    {
        proc.getEqCurve(curve, 600); spectrum.setEqCurve(curve, 600);
    }
    spectrum.repaint();
    padGrid.tick();

    if (auditionTimerRemaining > 0)
    {
        if (--auditionTimerRemaining == 0)
            proc.uiNoteOff(auditionNote);
    }
}


