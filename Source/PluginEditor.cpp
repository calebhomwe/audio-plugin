#include "PluginProcessor.h"
#include "PluginEditor.h"

MixAgentAudioProcessorEditor::MixAgentAudioProcessorEditor(MixAgentAudioProcessor& p)
    : AudioProcessorEditor(p), proc(p)
{
    setSize(1160, 640);

    logoLabel.setText("MIXAGENT", juce::dontSendNotification);
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
    presetCombo.setSelectedId(1);
    presetCombo.setColour(juce::ComboBox::backgroundColourId, agm::ui::kPanelHi);
    presetCombo.setColour(juce::ComboBox::textColourId, agm::ui::kText);
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

    hpToggle = addToggle("eq_hp_enabled", "HP");
    lpToggle = addToggle("eq_lp_enabled", "LP");
    monoToggle = addToggle("img_mono", "MONO");

    hpKnob = addKnob("eq_hp_freq", "HP", " Hz", 0);
    lpKnob = addKnob("eq_lp_freq", "LP", " Hz", 0);
    lsfF = addKnob("eq_lsf_freq", "LS F", " Hz", 0);
    lsfG = addKnob("eq_lsf_gain", "LS G", " dB", 1);
    hsfF = addKnob("eq_hsf_freq", "HS F", " Hz", 0);
    hsfG = addKnob("eq_hsf_gain", "HS G", " dB", 1);
    p1F = addKnob("eq_p1_freq", "P1 F", " Hz", 0);
    p1G = addKnob("eq_p1_gain", "P1 G", " dB", 1);
    p1Q = addKnob("eq_p1_q", "P1 Q", "", 2);
    p2F = addKnob("eq_p2_freq", "P2 F", " Hz", 0);
    p2G = addKnob("eq_p2_gain", "P2 G", " dB", 1);
    p2Q = addKnob("eq_p2_q", "P2 Q", "", 2);
    p3F = addKnob("eq_p3_freq", "P3 F", " Hz", 0);
    p3G = addKnob("eq_p3_gain", "P3 G", " dB", 1);
    p3Q = addKnob("eq_p3_q", "P3 Q", "", 2);

    satDrive = addKnob("sat_drive", "DRIVE", "", 0);
    satDrive->textFromValueFunction = [](double v) { return juce::String((int)(v * 100.0)) + "%"; };
    satMix = addKnob("sat_mix", "MIX", "", 0);
    satMix->textFromValueFunction = [](double v) { return juce::String((int)(v * 100.0)) + "%"; };
    satOut = addKnob("sat_out", "OUT", " dB", 1);

    compT = addKnob("comp_thresh", "THRESH", " dB", 0);
    compR = addKnob("comp_ratio", "RATIO", ":1", 1);
    compA = addKnob("comp_attack", "ATTACK", " ms", 0);
    compRel = addKnob("comp_release", "RELEASE", " ms", 0);
    compK = addKnob("comp_knee", "KNEE", " dB", 0);
    compMix = addKnob("comp_mix", "MIX", "", 0);
    compMix->textFromValueFunction = [](double v) { return juce::String((int)(v * 100.0)) + "%"; };
    compMake = addKnob("comp_makeup", "MAKEUP", " dB", 0);

    imgW = addKnob("img_width", "WIDTH", "", 0);
    imgW->textFromValueFunction = [](double v) { return juce::String((int)v) + "%"; };
    imgB = addKnob("img_balance", "BAL", "", 2);

    dlyT = addKnob("dly_time", "TIME", " ms", 0);
    dlyF = addKnob("dly_feedback", "FEEDBACK", "", 0);
    dlyF->textFromValueFunction = [](double v) { return juce::String((int)(v * 100.0)) + "%"; };
    dlyM = addKnob("dly_mix", "MIX", "", 0);
    dlyM->textFromValueFunction = [](double v) { return juce::String((int)(v * 100.0)) + "%"; };
    dlyD = addKnob("dly_damp", "DAMP", "", 0);
    dlyD->textFromValueFunction = [](double v) { return juce::String((int)(v * 100.0)) + "%"; };
    dlyW = addKnob("dly_width", "WIDTH", "", 0);
    dlyW->textFromValueFunction = [](double v) { return juce::String((int)(v * 100.0)) + "%"; };

    rvbS = addKnob("rvb_size", "SIZE", "", 2);
    rvbDc = addKnob("rvb_decay", "DECAY", " s", 1);
    rvbD = addKnob("rvb_damp", "DAMP", "", 0);
    rvbD->textFromValueFunction = [](double v) { return juce::String((int)(v * 100.0)) + "%"; };
    rvbW = addKnob("rvb_width", "WIDTH", "", 0);
    rvbW->textFromValueFunction = [](double v) { return juce::String((int)(v * 100.0)) + "%"; };
    rvbM = addKnob("rvb_mix", "MIX", "", 0);
    rvbM->textFromValueFunction = [](double v) { return juce::String((int)(v * 100.0)) + "%"; };
    rvbP = addKnob("rvb_predelay", "PRE-DELAY", " ms", 0);

    limC = addKnob("lim_ceiling", "CEILING", " dB", 1);
    limA = addKnob("lim_attack", "ATTACK", " ms", 1);
    limR = addKnob("lim_release", "RELEASE", " ms", 0);

    startTimerHz(20);
}

agm::ui::Knob* MixAgentAudioProcessorEditor::addKnob(const juce::String& id, const juce::String& label,
                                                     const juce::String& suffix, int decimals)
{
    auto* knob = knobs.add(new agm::ui::Knob(label, suffix));
    knob->setNumDecimalPlacesToDisplay(decimals);
    if (auto* p = dynamic_cast<juce::RangedAudioParameter*>(proc.getAPVTS().getParameter(id)))
    {
        knob->setRange(p->getNormalisableRange().start, p->getNormalisableRange().end, p->getNormalisableRange().interval);
        knob->setDoubleClickReturnValue(true, p->getDefaultValue());
        knobAttachments.add(new juce::AudioProcessorValueTreeState::SliderAttachment(proc.getAPVTS(), id, *knob));
    }
    addAndMakeVisible(knob);
    return knob;
}

juce::ToggleButton* MixAgentAudioProcessorEditor::addPower(const juce::String& id)
{
    auto* t = powerToggles.add(new juce::ToggleButton());
    styleToggle(*t, "");
    buttonAttachments.add(new juce::AudioProcessorValueTreeState::ButtonAttachment(proc.getAPVTS(), id, *t));
    addAndMakeVisible(t);
    return t;
}

juce::ToggleButton* MixAgentAudioProcessorEditor::addToggle(const juce::String& id, const juce::String& text)
{
    auto* t = new juce::ToggleButton();
    styleToggle(*t, text);
    buttonAttachments.add(new juce::AudioProcessorValueTreeState::ButtonAttachment(proc.getAPVTS(), id, *t));
    addAndMakeVisible(t);
    return t;
}

void MixAgentAudioProcessorEditor::styleToggle(juce::ToggleButton& t, const juce::String& text)
{
    t.setButtonText(text);
    t.setColour(juce::ToggleButton::tickColourId, agm::ui::kAccent);
    t.setColour(juce::ToggleButton::tickDisabledColourId, agm::ui::kBorder);
    t.setColour(juce::ToggleButton::textColourId, agm::ui::kTextDim);
}

void MixAgentAudioProcessorEditor::placeKnobs(float centreX, int y, const juce::Array<agm::ui::Knob*>& ks)
{
    const int gap = 4;
    const int totalW = ks.size() * 50 + ((int)ks.size() - 1) * gap;
    int x = (int)centreX - totalW / 2;
    for (auto* k : ks)
    {
        k->setBounds(x, y, 50, 72);
        x += 50 + gap;
    }
}

void MixAgentAudioProcessorEditor::drawHeader(juce::Graphics& g, juce::Rectangle<int> r, const char* name)
{
    g.setColour(agm::ui::kTextDim);
    g.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)));
    g.drawText(name, r.getX() + 10, r.getY() + 3, r.getWidth() - 20, 20, juce::Justification::centredLeft);
}

void MixAgentAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(agm::ui::kBg);

    auto panelPaint = [&](juce::Rectangle<int> r)
    {
        g.setColour(agm::ui::kPanel);
        g.fillRoundedRectangle(r.toFloat(), 6.0f);
        g.setColour(agm::ui::kBorder);
        g.drawRoundedRectangle(r.toFloat().reduced(0.5f), 6.0f, 1.0f);
        g.drawHorizontalLine(r.getY() + 27, (float)r.getX() + 8.0f, (float)r.getRight() - 8.0f);
    };

    panelPaint(topBarRect);
    panelPaint(eqSection);
    for (auto& p : panels)
        panelPaint(p);

    drawHeader(g, eqSection, "EQUALIZER");
    drawHeader(g, panels[0], "SATURATION");
    drawHeader(g, panels[1], "COMPRESSOR");
    drawHeader(g, panels[2], "STEREO IMAGER");
    drawHeader(g, panels[3], "DELAY");
    drawHeader(g, panels[4], "REVERB");
    drawHeader(g, panels[5], "LIMITER");
}

void MixAgentAudioProcessorEditor::resized()
{
    auto r = getLocalBounds().reduced(agm::ui::kMargin);
    topBarRect = r.removeFromTop(44);
    r.removeFromTop(10);
    eqSection = r.removeFromTop(230);
    r.removeFromTop(10);
    modulesRow = r;

    auto t = topBarRect.reduced(14, 0);
    logoLabel.setBounds(t.removeFromLeft(220).withY(topBarRect.getY() + 4).withHeight(36));
    subLabel.setBounds(t.removeFromLeft(190).withY(topBarRect.getY() + 18).withHeight(14));

    presetCombo.setBounds(t.removeFromRight(170).withSizeKeepingCentre(170, 24));
    t.removeFromRight(14);
    outLabel.setBounds(t.removeFromRight(30).withSizeKeepingCentre(30, 12));
    outMeter.setBounds(t.removeFromRight(14).withSizeKeepingCentre(14, 36));
    t.removeFromRight(12);
    inLabel.setBounds(t.removeFromRight(30).withSizeKeepingCentre(30, 12));
    inMeter.setBounds(t.removeFromRight(14).withSizeKeepingCentre(14, 36));

    powerToggles[0]->setBounds(eqSection.getRight() - 28, eqSection.getY() + 6, 18, 16);

    spectrum.setBounds(eqSection.getX() + 10, eqSection.getY() + 32, eqSection.getWidth() - 20, 96);

    const int knobY = eqSection.getY() + 138;
    const float cw = (float)eqSection.getWidth() / 7.0f;
    auto cx = [&](int i) { return eqSection.getX() + (i + 0.5f) * cw; };
    hpToggle->setBounds((int)cx(0) - 49, knobY + 24, 42, 20);
    hpKnob->setBounds((int)cx(0) - 2, knobY, 50, 72);
    lpToggle->setBounds((int)cx(6) - 49, knobY + 24, 42, 20);
    lpKnob->setBounds((int)cx(6) - 2, knobY, 50, 72);
    placeKnobs(cx(1), knobY, { lsfF, lsfG });
    placeKnobs(cx(2), knobY, { p1F, p1G, p1Q });
    placeKnobs(cx(3), knobY, { p2F, p2G, p2Q });
    placeKnobs(cx(4), knobY, { p3F, p3G, p3Q });
    placeKnobs(cx(5), knobY, { hsfF, hsfG });

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
        powerToggles[i + 1]->setBounds(panels[i].getRight() - 28, panels[i].getY() + 6, 18, 16);

    const int row1 = panelY + 36;
    const int row2 = row1 + 74;
    const int row3 = row2 + 74;

    placeKnobs(panels[0].getCentreX(), row1, { satDrive, satMix, satOut });
    satModeCombo.setBounds(panels[0].getX() + 30, row2 + 22, panels[0].getWidth() - 60, 22);

    auto compArea = panels[1].withTrimmedRight(30);
    compMeter.setBounds(panels[1].getRight() - 22, row1, 14, panels[1].getBottom() - row1 - 14);
    placeKnobs(compArea.getCentreX(), row1, { compT, compR, compA });
    placeKnobs(compArea.getCentreX(), row2, { compRel, compK, compMix });
    placeKnobs(compArea.getCentreX(), row3, { compMake });

    placeKnobs(panels[2].getCentreX(), row1, { imgW, imgB });
    monoToggle->setBounds(panels[2].getCentreX() - 30, row2 + 24, 60, 20);

    placeKnobs(panels[3].getCentreX(), row1, { dlyT, dlyF });
    placeKnobs(panels[3].getCentreX(), row2, { dlyM, dlyD });
    placeKnobs(panels[3].getCentreX(), row3, { dlyW });

    placeKnobs(panels[4].getCentreX(), row1, { rvbS, rvbDc });
    placeKnobs(panels[4].getCentreX(), row2, { rvbD, rvbW });
    placeKnobs(panels[4].getCentreX(), row3, { rvbM, rvbP });

    auto limArea = panels[5].withTrimmedRight(30);
    limMeter.setBounds(panels[5].getRight() - 22, row1, 14, panels[5].getBottom() - row1 - 14);
    placeKnobs(limArea.getCentreX(), row1, { limC });
    placeKnobs(limArea.getCentreX(), row2, { limA });
    placeKnobs(limArea.getCentreX(), row3, { limR });
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
}
