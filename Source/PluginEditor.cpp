#include "PluginProcessor.h"
#include "PluginEditor.h"

MixAgentAudioProcessorEditor::MixAgentAudioProcessorEditor(MixAgentAudioProcessor& p)
    : AudioProcessorEditor(p), proc(p)
{
    setSize(1160, 920);

    /* Component ids throughout: a visual rubric that reports "(Label) at 22,12 is
       off the grid" names nothing, and this editor had set no id on anything. */
    logoLabel.setComponentID("logo");
    subLabel.setComponentID("strapline");
    inLabel.setComponentID("label_in");
    outLabel.setComponentID("label_out");
    padLabel.setComponentID("label_pads");
    presetCombo.setComponentID("combo_preset");
    satModeCombo.setComponentID("combo_sat_mode");
    instProgramCombo.setComponentID("combo_inst_program");
    instFilterCombo.setComponentID("combo_inst_filter");
    favToggle.setComponentID("toggle_fav");
    inMeter.setComponentID("meter_in");
    outMeter.setComponentID("meter_out");
    compMeter.setComponentID("meter_comp_gr");
    limMeter.setComponentID("meter_lim_gr");
    spectrum.setComponentID("spectrum");
    padGrid.setComponentID("pad_grid");

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
    // dontSendNotification: ComboBox::setSelectedId notifies ASYNCHRONOUSLY by
    // default, so this fired presetCombo.onChange -> setCurrentProgram(0) a few
    // milliseconds after the editor opened. Now that selecting a preset resets
    // every parameter to its default, that would wipe the user's settings just
    // for opening the window.
    presetCombo.setSelectedId(1, juce::dontSendNotification);
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
    addAndMakeVisible(compMeter);
    addAndMakeVisible(limMeter);

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

    hpKnob = addKnob("eq_hp_freq", "HP", " Hz", 0);
    hpKnob->textFromValueFunction = hzText;
    lpKnob = addKnob("eq_lp_freq", "LP", " Hz", 0);
    lpKnob->textFromValueFunction = hzText;
    lsfF = addKnob("eq_lsf_freq", "LS F", " Hz", 0);
    lsfF->textFromValueFunction = hzText;
    lsfG = addKnob("eq_lsf_gain", "LS G", " dB", 1);
    hsfF = addKnob("eq_hsf_freq", "HS F", " Hz", 0);
    hsfF->textFromValueFunction = hzText;
    hsfG = addKnob("eq_hsf_gain", "HS G", " dB", 1);
    p1F = addKnob("eq_p1_freq", "P1 F", " Hz", 0);
    p1F->textFromValueFunction = hzText;
    p1G = addKnob("eq_p1_gain", "P1 G", " dB", 1);
   #if AGM_MUTATION(25)
    // M25: a readout that prints the float it happens to hold.
    p1Q = addKnob("eq_p1_q", "P1 Q", "", 7);
   #else
    p1Q = addKnob("eq_p1_q", "P1 Q", "", 2);
   #endif
    p2F = addKnob("eq_p2_freq", "P2 F", " Hz", 0);
    p2F->textFromValueFunction = hzText;
    p2G = addKnob("eq_p2_gain", "P2 G", " dB", 1);
    p2Q = addKnob("eq_p2_q", "P2 Q", "", 2);
    p3F = addKnob("eq_p3_freq", "P3 F", " Hz", 0);
    p3F->textFromValueFunction = hzText;
    p3G = addKnob("eq_p3_gain", "P3 G", " dB", 1);
    p3Q = addKnob("eq_p3_q", "P3 Q", "", 2);

    satDrive = addKnob("sat_drive", "DRIVE", " %", 0, 100.0f);
    satDrive->textFromValueFunction = pctText;
    satMix = addKnob("sat_mix", "MIX", " %", 0, 100.0f);
    satMix->textFromValueFunction = pctText;
    satOut = addKnob("sat_out", "OUT", " dB", 1);

   #if AGM_MUTATION(26)
    // M26: the compressor threshold loses its unit.
    compT = addKnob("comp_thresh", "THRESH", "", 0);
   #else
    compT = addKnob("comp_thresh", "THRESH", " dB", 0);
   #endif
    compR = addKnob("comp_ratio", "RATIO", ":1", 1);
    /* 0.1..100 ms at 0 decimals printed "0 ms" at the bottom of its range: a
       readout that cannot show its own value. The sibling of the defect this
       repository already fixed once ("ten controls with a 0..1 range were drawn
       with zero decimal places"). */
    compA = addKnob("comp_attack", "ATTACK", " ms", 1);
    compRel = addKnob("comp_release", "RELEASE", " ms", 0);
    compK = addKnob("comp_knee", "KNEE", " dB", 0);
    compMix = addKnob("comp_mix", "MIX", " %", 0, 100.0f);
    compMix->textFromValueFunction = pctText;
    compMake = addKnob("comp_makeup", "MAKEUP", " dB", 0);

    imgW = addKnob("img_width", "WIDTH", " %", 0);
    imgW->textFromValueFunction = [](double v) { return juce::String(juce::roundToInt(v)) + "%"; };
    imgB = addKnob("img_balance", "BAL", "", 2);

    dlyT = addKnob("dly_time", "TIME", " ms", 0);
    dlyF = addKnob("dly_feedback", "FEEDBACK", " %", 0, 100.0f);
    dlyF->textFromValueFunction = pctText;
    dlyM = addKnob("dly_mix", "MIX", " %", 0, 100.0f);
    dlyM->textFromValueFunction = pctText;
    dlyD = addKnob("dly_damp", "DAMP", " %", 0, 100.0f);
    dlyD->textFromValueFunction = pctText;
    dlyW = addKnob("dly_width", "WIDTH", " %", 0, 100.0f);
    dlyW->textFromValueFunction = pctText;

    rvbS = addKnob("rvb_size", "SIZE", " %", 0, 100.0f);
    /* Was " s", 1 decimal, beside PRE-DELAY in " ms" - two time units in one
       panel, which is the defect that makes two times incomparable at a glance.
       The scale argument converts for the READOUT only; the parameter is still
       seconds. 0.2..10 s reads 200..10000 ms. */
   #if AGM_MUTATION(27)
    // M27: DECAY back to seconds beside PRE-DELAY in milliseconds.
    rvbDc = addKnob("rvb_decay", "DECAY", " s", 1);
   #else
    rvbDc = addKnob("rvb_decay", "DECAY", " ms", 0, 1000.0f);
   #endif
    rvbD = addKnob("rvb_damp", "DAMP", " %", 0, 100.0f);
    rvbD->textFromValueFunction = pctText;
    rvbW = addKnob("rvb_width", "WIDTH", " %", 0, 100.0f);
    rvbW->textFromValueFunction = pctText;
    rvbM = addKnob("rvb_mix", "MIX", " %", 0, 100.0f);
    rvbM->textFromValueFunction = pctText;
    rvbP = addKnob("rvb_predelay", "PRE-DELAY", " ms", 0);

    limC = addKnob("lim_ceiling", "CEILING", " dB", 1);
    /* 0.01..10 ms at 1 decimal printed "0.0 ms" at the bottom of its range. */
    limA = addKnob("lim_attack", "ATTACK", " ms", 2);
    limR = addKnob("lim_release", "RELEASE", " ms", 0);

   #if AGM_MUTATION(31)
    // M31: a tenth font size on the editor - 9.25 px for one label.
    padLabel.setFont(juce::Font(juce::FontOptions(9.25f, juce::Font::bold)));
   #else
    padLabel.setFont(juce::Font(juce::FontOptions(9.0f, juce::Font::bold)));
   #endif
    padLabel.setColour(juce::Label::textColourId, agm::ui::kTextDim);
    padLabel.setJustificationType(juce::Justification::centredRight);
   #if AGM_MUTATION(23)
    // M23: the pad strip's caption given more words than its box holds.
    padLabel.setText("16 INSTRUMENTS, 12 CHROMATIC PREVIEW KEYS AND A DRUM BANK - PLAY THEM "
                     "WITH MIDI, WITH THE MOUSE, OR WITH THE COMPUTER KEYBOARD",
                     juce::dontSendNotification);
   #else
    padLabel.setText("16 INSTRUMENTS - MIDI OR MOUSE", juce::dontSendNotification);
   #endif
    addAndMakeVisible(padLabel);

     for (int i = 0; i < (int)agm::InstrumentBank::kCount; ++i)
        instProgramCombo.addItem(agm::InstrumentBank::programName(i), i + 1);
    instProgramCombo.setSelectedId(1, juce::dontSendNotification);
    instProgramCombo.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff0a0a0d));
    instProgramCombo.setColour(juce::ComboBox::textColourId, agm::ui::kAccentHot);
    instProgramCombo.setColour(juce::ComboBox::arrowColourId, agm::ui::kTextDim);
    instProgramCombo.setColour(juce::ComboBox::outlineColourId, agm::ui::kBorder);
    instProgramAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        proc.getAPVTS(), "inst_program", instProgramCombo);
    instProgramCombo.onChange = [this]
    {
        // audition: fire a middle-C pluck so the program is heard instantly
        proc.uiNoteOn(60, 0.85f);
        auditionTimerRemaining = 30; // noteOff after ~300ms (timer ticks at 20Hz)
        const int item = instProgramCombo.getSelectedId();
        if (item >= 1 && item <= (int)agm::InstrumentBank::kCount)
            favToggle.setToggleState(proc.isFavorite(item - 1), juce::dontSendNotification);
    };
    addAndMakeVisible(instProgramCombo);

    instFilterCombo.addItem("ALL", 1);
    instFilterCombo.addItem("FAVORITES", 2);
    instFilterCombo.addItem("KEYS/LEAD", 3);
    instFilterCombo.addItem("BASS", 4);
    instFilterCombo.setSelectedId(1, juce::dontSendNotification);
    instFilterCombo.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff0a0a0d));
    instFilterCombo.setColour(juce::ComboBox::textColourId, agm::ui::kAccentHot);
    instFilterCombo.setColour(juce::ComboBox::arrowColourId, agm::ui::kTextDim);
    instFilterCombo.setColour(juce::ComboBox::outlineColourId, agm::ui::kBorder);
    instFilterCombo.onChange = [this] { updateProgramList(); };
    addAndMakeVisible(instFilterCombo);

    favToggle.setColour(juce::ToggleButton::textColourId, agm::ui::kAccent);
    favToggle.onClick = [this] { onClickFav(); };
    addAndMakeVisible(favToggle);

    // in_gain and out_gain were parameters with no control anywhere on the
    // editor either: the meters were there, the trims were not.
    inTrim = addKnob("in_gain", "IN", " dB", 1);
    outTrim = addKnob("out_gain", "OUT", " dB", 1);

    instLevel = addKnob("inst_level", "LEVEL", " dB", 1);
    // drum_level was a fully working parameter with no control anywhere on the
    // editor: reachable only through host automation.
    drumLevel = addKnob("drum_level", "DRUMS", " dB", 1);

    // chromatic preview keyboard (one octave, triggers instruments via UI queue)
    std::vector<agm::ui::PadGrid::Pad> keys;
    static const char* noteNames[] = { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };
    for (int i = 0; i < 12; ++i)
       #if AGM_MUTATION(32)
        /* M32: an eighth hue on the editor - every other preview key turned magenta.
           Two earlier attempts did not cross the threshold, and both were findings
           about the mutation rather than about the rule: turning them ALL violet
           REPLACED the pad blue instead of adding to it and left the count at seven,
           and 0xff7d2fff is hue 262 deg, which falls in the SAME 30 deg bucket as the
           navy panels. 0xffcc2fff is 285 deg, a bucket nothing else occupies. */
        keys.push_back({ noteNames[i], 48 + i,
                         juce::Colour(i % 2 == 0 ? 0xffcc2fff : 0xff2f7dff) });
       #else
        keys.push_back({ noteNames[i], 48 + i, juce::Colour(0xff2f7dff) });
       #endif
    padGrid.setPads(std::move(keys));
    addAndMakeVisible(padGrid);

    startTimerHz(20);
    fullyBuilt = true;
    resized();
}

agm::ui::Knob* MixAgentAudioProcessorEditor::addKnob(const juce::String& id, const juce::String& label,
                                                     const juce::String& suffix, int decimals,
                                                     float displayScale)
{
    auto* knob = knobs.add(new agm::ui::Knob(label, suffix, displayScale, decimals));
    knob->setComponentID("knob_" + id);
    knob->setNumDecimalPlacesToDisplay(decimals);
    if (auto* p = dynamic_cast<juce::RangedAudioParameter*>(proc.getAPVTS().getParameter(id)))
    {
        knob->setRange(p->getNormalisableRange().start, p->getNormalisableRange().end, p->getNormalisableRange().interval);
        // getDefaultValue() is normalised, so it has to be converted. This call
        // is belt-and-braces: juce::SliderParameterAttachment sets the same value
        // a line later, which is why double-click-to-default was already correct.
        // EditorProbe asserts it, so a future change to either cannot break it
        // silently.
        knob->setDoubleClickReturnValue(true, p->getNormalisableRange().convertFrom0to1(p->getDefaultValue()));
        knobAttachments.add(new juce::AudioProcessorValueTreeState::SliderAttachment(proc.getAPVTS(), id, *knob));
    }
    addAndMakeVisible(knob);
    return knob;
}

juce::ToggleButton* MixAgentAudioProcessorEditor::addPower(const juce::String& id)
{
    auto* t = powerToggles.add(new agm::ui::PowerToggle());
    t->setComponentID("power_" + id);
    buttonAttachments.add(new juce::AudioProcessorValueTreeState::ButtonAttachment(proc.getAPVTS(), id, *t));
    addAndMakeVisible(t);
    return t;
}

juce::ToggleButton* MixAgentAudioProcessorEditor::addToggle(const juce::String& id, const juce::String& text)
{
    auto* t = new agm::ui::PowerToggle(text);
    t->setComponentID("toggle_" + id);
    buttonAttachments.add(new juce::AudioProcessorValueTreeState::ButtonAttachment(proc.getAPVTS(), id, *t));
    addAndMakeVisible(t);
    return t;
}

void MixAgentAudioProcessorEditor::updateProgramList()
{
    const int filter = instFilterCombo.getSelectedId();
    const int currentId = instProgramCombo.getSelectedId();
    instProgramCombo.clear(juce::dontSendNotification);

    for (int i = 0; i < (int)agm::InstrumentBank::kCount; ++i)
    {
        const juce::String cat = agm::InstrumentBank::programCategory(i);
        const bool matchesFav = (filter != 2) || proc.isFavorite(i);
        const bool matchesCat = (filter != 3 || cat == "Keys/Lead") && (filter != 4 || cat == "Bass");
        if (matchesFav && matchesCat)
            instProgramCombo.addItem(agm::InstrumentBank::programName(i), i + 1);
    }
    instProgramCombo.setSelectedId(juce::jmax(currentId, 1), juce::dontSendNotification);

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

    /* Every string this editor paints for itself comes from paintedTexts(), which
       is also what the rubric measures. The underline each section header carries
       is drawn from the same geometry. */
    for (const auto& t : paintedTexts())
    {
        g.setColour(t.colour);
        g.setFont(t.font);
        g.drawText(t.text, t.bounds, t.just, false);
        if (t.panel.isNotEmpty() && t.text != "MODE" && t.text != "MIXAGENT")
        {
            const float w = juce::GlyphArrangement::getStringWidth(t.font, t.text);
            const float ux = (float)t.bounds.getX();
            const float uy = (float)t.bounds.getBottom() + 1.0f;
            g.setGradientFill(juce::ColourGradient(agm::ui::kAccent, ux, uy,
                                                   agm::ui::kAccent.withAlpha(0.0f),
                                                   ux + w + 34.0f, uy, false));
            g.fillRect(ux, uy, w + 34.0f, 2.0f);
        }
    }
}

/* ------------------------------------------------------------------------- */
/* The eight headers, the logo and the MODE caption, with the rectangle and the
   font each one is actually drawn with. */
juce::Array<MixAgentAudioProcessorEditor::PaintedText>
MixAgentAudioProcessorEditor::paintedTexts() const
{
    const juce::Font header(juce::FontOptions(11.0f, juce::Font::bold));
    juce::Array<PaintedText> out;
    auto addHeader = [&] (juce::Rectangle<int> r, const char* name, const char* panel,
                          int inset, juce::Colour colour)
    {
        out.add({ name, { r.getX() + inset, r.getY() + 3, r.getWidth() - inset - 10, 20 },
                  header, colour, panel });
    };
    out.add({ "MIXAGENT",
              topBarRect.reduced(14, 0).withWidth(220).withY(topBarRect.getY() + 4).withHeight(36),
              juce::Font(juce::FontOptions(24.0f, juce::Font::bold)), agm::ui::kText, "top bar" });
    addHeader(eqSection, "EQUALIZER",     "equaliser", 10, agm::ui::kTextDim);
    addHeader(panels[0], "SATURATION",    "saturation", 10, agm::ui::kTextDim);
    addHeader(panels[1], "COMPRESSOR",    "compressor", 10, agm::ui::kTextDim);
    addHeader(panels[2], "STEREO IMAGER", "imager",     10, agm::ui::kTextDim);
    addHeader(panels[3], "DELAY",         "delay",      10, agm::ui::kTextDim);
    addHeader(panels[4], "REVERB",        "reverb",     10, agm::ui::kTextDim);
    addHeader(panels[5], "LIMITER",       "limiter",    10, agm::ui::kTextDim);
    addHeader(drumRect,  "INSTRUMENT LIBRARY", "instruments", 34, agm::ui::kAccentHot);
    const auto combo = satModeCombo.getBounds();
    if (combo.getHeight() > 0)
        out.add({ "MODE", combo.withHeight(12).withY(combo.getY() - 14),
                  juce::Font(juce::FontOptions(8.5f, juce::Font::bold)), agm::ui::kTextDim,
                  "saturation", juce::Justification::centred });
    return out;
}

/* The painted graphics R7 is measured on, each foreground paired with the colour
   it is actually drawn OVER so an alpha is composited rather than assumed away. */
juce::Array<MixAgentAudioProcessorEditor::GraphicSample>
MixAgentAudioProcessorEditor::graphicSamples() const
{
    using namespace agm::ui;
    const juce::Colour panelMid = kPanelHi.interpolatedWith(kPanel, 0.5f);
    juce::Array<GraphicSample> out;
    /* surface:   a fill, not something to read.
       structural: a panel border, a bevel, a knob's unfilled groove, its tick ring.
                   Reported, not asserted - see Tests/VisualRubric.h.
       otherwise:  an INDICATOR: it carries state, a value or a scale, and R7 is
                   asserted on it. */
    out.add({ "panel fill over canvas",    panelMid,   kBg,      true  });
    out.add({ "panel border",              kBorder,    panelMid, false, true });
    out.add({ "panel top highlight",       juce::Colours::white.withAlpha(0.25f), panelMid, false, true });
    out.add({ "header rule under a title", kAccent,    panelMid, false });
    out.add({ "power LED, off",            PowerToggle::ledOffColour(),  panelMid, false });
    out.add({ "power LED, on",             PowerToggle::ledOnColour(),   panelMid, false });
    out.add({ "power ring, off",           PowerToggle::ringOffColour(), panelMid, false });
    out.add({ "drum panel cyan rule",      kCyan,      panelMid, false });
    out.add({ "pad, lit",                  kPadLit,    kPanel,   false });
    /* The knobs' and the meters' own colours come from the widgets that paint them.
       A colour literal inside Knob::paint or Meter::paint plus a separate list here
       is two sources of truth, and a mutation of the gain-reduction tick survived R7
       untouched while they were separate: the table still named the colour the rule
       wanted to see. */
    for (const auto& cp : Knob::indicatorColours())
        out.add({ cp.what, cp.fg, cp.bg, false, cp.structural });
    for (const auto& cp : inMeter.indicatorColours())
        out.add({ cp.what, cp.fg, cp.bg, false, cp.structural });
    for (const auto& cp : compMeter.indicatorColours())
        out.add({ cp.what, cp.fg, cp.bg, false, cp.structural });
    return out;
}

juce::Array<MixAgentAudioProcessorEditor::MeterRef>
MixAgentAudioProcessorEditor::meterRefs() const
{
    juce::Array<MeterRef> out;
    out.add({ "input level meter",            &inMeter });
    out.add({ "output level meter",           &outMeter });
    out.add({ "compressor gain reduction",    &compMeter });
    out.add({ "limiter gain reduction",       &limMeter });
    return out;
}

/* Which section a child sits in: the rectangle that contains its centre. R2's
   "time units are consistent within a panel" has no meaning without this. */
juce::String MixAgentAudioProcessorEditor::panelOf (const juce::Component& c) const
{
    const auto centre = c.getBounds().getCentre();
    if (topBarRect.contains(centre)) return "top bar";
    if (eqSection.contains(centre))  return "equaliser";
    if (drumRect.contains(centre))   return "instruments";
    static const char* const names[6] = { "saturation", "compressor", "imager",
                                          "delay", "reverb", "limiter" };
    for (int i = 0; i < 6; ++i)
        if (panels[i].contains(centre)) return names[i];
    return "unplaced";
}

void MixAgentAudioProcessorEditor::resized()
{
    if (!fullyBuilt)
        return;
    auto r = getLocalBounds().reduced(agm::ui::kMargin);
    topBarRect = r.removeFromTop(44);
    r.removeFromTop(10);
    eqSection = r.removeFromTop(230);
    r.removeFromTop(10);
    const auto drumRow = r.removeFromBottom(170);
    r.removeFromTop(10);
    modulesRow = r;
    drumRect = drumRow;

    auto t = topBarRect.reduced(14, 0);
    logoLabel.setBounds(t.removeFromLeft(220).withY(topBarRect.getY() + 4).withHeight(36));
    subLabel.setBounds(t.removeFromLeft(190).withY(topBarRect.getY() + 18).withHeight(14));

    presetCombo.setBounds(t.removeFromRight(170).withSizeKeepingCentre(170, 26));
    t.removeFromRight(16);
    outTrim->setBounds(t.removeFromRight(50).withSizeKeepingCentre(50, 42).withY(topBarRect.getY() + 1));
    outLabel.setBounds(t.removeFromRight(30).withSizeKeepingCentre(30, 12));
    outMeter.setBounds(t.removeFromRight(14).withSizeKeepingCentre(14, 36));
    t.removeFromRight(10);
    inTrim->setBounds(t.removeFromRight(50).withSizeKeepingCentre(50, 42).withY(topBarRect.getY() + 1));
    inLabel.setBounds(t.removeFromRight(30).withSizeKeepingCentre(30, 12));
    inMeter.setBounds(t.removeFromRight(14).withSizeKeepingCentre(14, 36));

    /* 24x24, not 18x16. Thirteen controls on this editor were under the 24 px hit
       target floor and the eight section power switches were the smallest at
       18x16. The header band is 27 px tall, so 24 px at y+2 still sits inside it. */
   #if AGM_MUTATION(28)
    // M28: the EQ power switch back to 18x16, under the 24 px hit-target floor.
    powerToggles[0]->setBounds(eqSection.getRight() - 30, eqSection.getY() + 7, 18, 16);
   #else
    powerToggles[0]->setBounds(eqSection.getRight() - 34, eqSection.getY() + 2, 24, 24);
   #endif

   #if AGM_MUTATION(34)
    // M34: the EQ display and its knob row are never laid out, so the whole
    // equaliser section is empty canvas.
    spectrum.setBounds(0, 0, 0, 0);
   #else
    spectrum.setBounds(eqSection.getX() + 10, eqSection.getY() + 32, eqSection.getWidth() - 20, 98);
   #endif

    const int knobY = eqSection.getY() + 138;
    const float cw = (float)eqSection.getWidth() / 7.0f;
    auto cx = [&](int i) { return eqSection.getX() + (i + 0.5f) * cw; };

   #if AGM_MUTATION(34)
    for (auto* k : { hpKnob, lpKnob, lsfF, lsfG, hsfF, hsfG, p1F, p1G, p1Q, p2F, p2G, p2Q, p3F, p3G, p3Q })
        k->setBounds(0, 0, 0, 0);
    hpToggle->setBounds(0, 0, 0, 0);
    lpToggle->setBounds(0, 0, 0, 0);
   #else
    hpKnob->setBounds((int)cx(0) - 2, knobY, 50, 72);
    hpToggle->setBounds((int)cx(0) - 56, knobY + 24, 48, 24);
    lpKnob->setBounds((int)cx(6) - 2, knobY, 50, 72);
    lpToggle->setBounds((int)cx(6) - 56, knobY + 24, 48, 24);
    placeKnobs(cx(1), knobY, { lsfF, lsfG });
    placeKnobs(cx(2), knobY, { p1F, p1G, p1Q });
    placeKnobs(cx(3), knobY, { p2F, p2G, p2Q });
    placeKnobs(cx(4), knobY, { p3F, p3G, p3Q });
    placeKnobs(cx(5), knobY, { hsfF, hsfG });
   #endif

    const int panelGap = 10;
    const int panelY = modulesRow.getY();
    const int panelH = modulesRow.getHeight();
    int px = modulesRow.getX();
    const int widths[6] = { 186, 210, 186, 186, 186, 140 };
    for (int i = 0; i < 6; ++i)
    {
        panels[i] = juce::Rectangle<int>(px, panelY, widths[i], panelH);
        px += widths[i] + panelGap;
    }
    for (int i = 0; i < 6; ++i)
        powerToggles[i + 1]->setBounds(panels[i].getRight() - 30, panels[i].getY() + 2, 24, 24);

    const int kw = 54, kh = 78;
    const int row1 = panelY + 34;
    const int row2 = row1 + 105;
    const int row3 = row2 + 105;
    const int contentH = panelH - 34 - 10;

    {
        const int blockH = kh + 32 + 26;
        const int sy = row1 + (contentH - blockH) / 2;
        placeKnobs(panels[0].getCentreX(), sy, { satDrive, satMix, satOut }, kw, kh);
        satModeCombo.setBounds(panels[0].getX() + 26, sy + kh + 32, panels[0].getWidth() - 52, 26);
    }

    auto compArea = panels[1].withTrimmedRight(32);
    compMeter.setBounds(panels[1].getRight() - 28, row1, 20, contentH);
    placeKnobs(compArea.getCentreX(), row1, { compT, compR, compA }, kw, kh);
    placeKnobs(compArea.getCentreX(), row2, { compRel, compK, compMix }, kw, kh);
    placeKnobs(compArea.getCentreX(), row3, { compMake }, kw, kh);

    {
        const int blockH = kh + 16 + 24;
        const int iy = row1 + (contentH - blockH) / 2;
        placeKnobs(panels[2].getCentreX(), iy, { imgW, imgB }, kw, kh);
        monoToggle->setBounds(panels[2].getCentreX() - 32, iy + kh + 16, 64, 24);
    }

    placeKnobs(panels[3].getCentreX(), row1, { dlyT, dlyF }, kw, kh);
    placeKnobs(panels[3].getCentreX(), row2, { dlyM, dlyD }, kw, kh);
    placeKnobs(panels[3].getCentreX(), row3, { dlyW }, kw, kh);

    placeKnobs(panels[4].getCentreX(), row1, { rvbS, rvbDc }, kw, kh);
    placeKnobs(panels[4].getCentreX(), row2, { rvbD, rvbW }, kw, kh);
    placeKnobs(panels[4].getCentreX(), row3, { rvbM, rvbP }, kw, kh);

    auto limArea = panels[5].withTrimmedRight(32);
    limMeter.setBounds(panels[5].getRight() - 28, row1, 20, contentH);
    placeKnobs(limArea.getCentreX(), row1, { limC }, kw, kh);
    placeKnobs(limArea.getCentreX(), row2, { limA }, kw, kh);
   #if AGM_MUTATION(30)
    // M30: the limiter's RELEASE knob pushed 6 px past the right edge of the window.
    limR->setBounds(getWidth() - kw + 6, row3, kw, kh);
   #else
    placeKnobs(limArea.getCentreX(), row3, { limR }, kw, kh);
   #endif

    powerToggles[7]->setBounds(drumRow.getX() + 6, drumRow.getY() + 2, 24, 24);
    instProgramCombo.setBounds(drumRow.getX() + 210, drumRow.getY() + 3, 110, 24);
   #if AGM_MUTATION(24)
    // M24: the filter combo box moved one pixel into the program combo box beside it.
    instFilterCombo.setBounds(drumRow.getX() + 319, drumRow.getY() + 3, 76, 24);
   #else
    instFilterCombo.setBounds(drumRow.getX() + 326, drumRow.getY() + 3, 76, 24);
   #endif
    favToggle.setBounds(drumRow.getX() + 408, drumRow.getY() + 2, 58, 26);
    instLevel->setBounds(drumRow.getX() + 474, drumRow.getY() + 2, 54, 64);
    drumLevel->setBounds(drumRow.getX() + 530, drumRow.getY() + 2, 54, 64);
    padLabel.setBounds(drumRow.getX() + 594, drumRow.getY() + 7, drumRow.getWidth() - 604, 16);
   #if AGM_MUTATION(21)
    // M21: the pad grid shifted 3 px off the 4 px grid. The off-grid COUNT cannot
    // see this - all 76 laid-out components are already off the grid - which is why
    // the probe also measures the total off-grid RESIDUAL, a quantity that is not
    // saturated and can therefore move in both directions.
    padGrid.setBounds(drumRow.withTrimmedTop(72).reduced(8, 6).translated(0, 3));
   #else
    padGrid.setBounds(drumRow.withTrimmedTop(72).reduced(8, 6));
   #endif
}

void MixAgentAudioProcessorEditor::timerCallback()
{
    inMeter.setLevel(proc.getInLevel(0), proc.getInLevel(1));
    outMeter.setLevel(proc.getOutLevel(0), proc.getOutLevel(1));
    compMeter.setGrDb(proc.getCompGrDb());
    limMeter.setGrDb(proc.getLimGrDb());

    float spec[600];
    float curve[600];
    proc.getAnalyzerSpectrum(spec, 600);
    proc.getEqCurve(curve, 600);
    spectrum.setSpectrum(spec, 600);
    spectrum.setEqCurve(curve, 600);
    spectrum.repaint();
    padGrid.tick();

    if (auditionTimerRemaining > 0)
    {
        if (--auditionTimerRemaining == 0)
            proc.uiNoteOff(60);
    }
}


