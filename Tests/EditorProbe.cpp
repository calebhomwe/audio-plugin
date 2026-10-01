#include <JuceHeader.h>
#include "Source/PluginProcessor.h"
#include "Source/PluginEditor.h"
#include "Tests/VisualRubric.h"
#include <algorithm>

// Headless editor smoke test: constructs the editor inside a window, drives a
// resize, renders a snapshot, then drives the program ComboBox and the knobs and
// checks that the attached parameters actually move (editor <-> APVTS mapping).
// Needs a display (on Linux: xvfb-run -a ./EditorProbe). Exit code 0 = all OK.
//
// Wave 6a: and then MEASURES WHAT THE EDITOR LOOKS LIKE. Up to now this file
// rendered a 1160x920 snapshot and asserted its WIDTH AND HEIGHT - which is
// "setBounds worked", not "the editor is legible". Tests/VisualRubric.h keeps the
// pixels and measures the thirteen numeric rules of the wave-6a rubric on them and
// on the component tree.
//
// This editor is a FIXED 1160x920, so there is no size axis to sweep. Its axis of
// variation is the PARAMETER VALUE: sixty-one knobs whose readouts change width
// with their value, in boxes 52 px wide. A readout that fits at its default and
// is condensed to illegibility at its maximum is exactly the R3 defect, and it is
// invisible at the defaults. So the rubric is measured at THREE parameter states:
// every parameter at its default, at its minimum, and at its maximum.
//
// AGM_SHOT_DIR writes the PNG and a JSON dump of the component tree per state.
// Off by default and free when off; the measurement is never gated.
namespace
{
int failures = 0;

void check(bool ok, const char* what)
{
    std::cout << (ok ? "[PASS] " : "[FAIL] ") << what << "\n";
    if (!ok) ++failures;
}

template <typename T>
void collect(juce::Component& root, std::vector<T*>& out)
{
    for (int i = 0; i < root.getNumChildComponents(); ++i)
    {
        auto* c = root.getChildComponent(i);
        if (auto* t = dynamic_cast<T*>(c)) out.push_back(t);
        collect<T>(*c, out);
    }
}


// ---------------------------------------------------------------------------
// Wave 6a: the visual rubric.
const char* vmsg(const juce::String& s)
{
    static std::vector<std::string> keep;
    keep.reserve(256);
    keep.push_back(s.toStdString());
    return keep.back().c_str();
}

// Readouts whose quantity genuinely has no unit. R2 requires a unit on every
// numeric readout; Q, a ratio and a left/right balance are dimensionless, and the
// caption above each one names the quantity. The exemption is a NAMED LIST, and
// the run prints how many items it covered, so it is visible rather than a hole.
bool isDimensionless(const juce::String& caption)
{
    return caption.endsWith("Q") || caption == "BAL" || caption == "RATIO";
}

void buildScene(MixAgentAudioProcessorEditor& ed, fvvis::Scene& sc, const juce::String& stateName,
                int& dimensionlessExempted, float& worstCondense, juce::String& worstCondenseWhat)
{
    dimensionlessExempted = 0;
    worstCondense = 1.0f;
    // A knob paints its own caption and readout; a PadGrid takes the mouse without
    // being a Slider. Both have to be described or the rubric cannot see them.
    sc.isInteractive = [](juce::Component& c)
    {
        return dynamic_cast<agm::ui::PadGrid*>(&c) != nullptr;
    };
    sc.describe = [&ed, &dimensionlessExempted, &worstCondense, &worstCondenseWhat]
                  (juce::Component& c, juce::Rectangle<int> abs, fvvis::Scene& scene)
    {
        if (auto* pt = dynamic_cast<agm::ui::PowerToggle*>(&c))
        {
            if (pt->getButtonText().isEmpty())
                return true;        // a bare LED paints no text at all
            fvvis::TextItem ti;
            ti.text = pt->getButtonText();
            ti.bounds = pt->textBox() + abs.getPosition();
            ti.font = agm::ui::PowerToggle::textFont();
            ti.colour = pt->findColour(juce::ToggleButton::textColourId);
            ti.just = juce::Justification::centredLeft;
            ti.owner = pt->getComponentID().isNotEmpty() ? pt->getComponentID() : pt->getButtonText();
            ti.panel = ed.panelOf(c);
            scene.addText(std::move(ti));
            return true;
        }
        auto* k = dynamic_cast<agm::ui::Knob*>(&c);
        if (k == nullptr)
            return false;
        const auto panel = ed.panelOf(c);
        juce::String caption;
        for (const auto& t : k->drawnTexts())
            if (!t.isReadout) caption = t.text;
        for (const auto& t : k->drawnTexts())
        {
            if (t.text.isEmpty()) continue;
            fvvis::TextItem ti;
            ti.text = t.text;
            ti.bounds = t.bounds + abs.getPosition();
            ti.font = t.font;
            ti.colour = t.colour;
            ti.just = juce::Justification::centred;
            ti.owner = (t.isReadout ? "readout:" : "caption:") + caption;
            ti.panel = panel;
            ti.readout = t.isReadout;
            if (t.isReadout && isDimensionless(caption))
            {
                ti.readout = false;
                ++dimensionlessExempted;
            }
            scene.addText(std::move(ti));
            if (t.condensed < worstCondense)
            {
                worstCondense = t.condensed;
                worstCondenseWhat = caption + " \"" + t.text + "\"";
            }
        }
        return true;    // the knob's own text, not the Slider default
    };

    sc.build(ed, "mixagent-" + stateName);

    for (const auto& t : ed.paintedTexts())
    {
        fvvis::TextItem ti;
        ti.text = t.text; ti.bounds = t.bounds; ti.font = t.font; ti.colour = t.colour;
        ti.just = t.just;
        ti.owner = "paint:" + t.text; ti.panel = t.panel; ti.painted = true;
        sc.addText(std::move(ti));
    }
    for (const auto& gp : ed.graphicSamples())
        sc.addGraphic({ gp.what, gp.fg, gp.bg, gp.surface, gp.structural });
    for (const auto& m : ed.meterRefs())
    {
        const auto info = m.meter->scaleInfo();
        fvvis::MeterInfo mi;
        mi.id = m.what + " (" + info.note + ")";
        mi.bounds = m.meter->getBounds();
        mi.labelledTicks = info.labelledTicks;
        mi.referenceMark = info.referenceMark;
        sc.addMeter(std::move(mi));
    }
    // Everything the tree walk found that is not a knob still needs its panel, so
    // R2's per-panel time-unit rule has something to group by.
    for (auto& t : sc.texts)
        if (t.panel.isEmpty()) t.panel = "unplaced";
}

void reportScene(const fvvis::Scene& sc, const std::vector<fvvis::Finding>& f,
                 int dimensionlessExempted, float worstCondense, const juce::String& worstCondenseWhat)
{
    std::cout << "  --- " << sc.name << "  canvas " << sc.canvas.getWidth() << "x"
              << sc.canvas.getHeight() << "  " << sc.nodes.size() << " components, "
              << sc.texts.size() << " text items, " << sc.meters.size() << " meters ---\n";
    std::cout << "  golden pixel hash " << sc.name << " = "
              << juce::String::toHexString((juce::int64)sc.pixelHash()) << "\n";
    for (const auto& x : f)
        std::cout << "  " << x.rule << (x.pass ? "  ok   " : "  FAIL ")
                  << "measured " << juce::String(x.measured, 2)
                  << "  threshold " << juce::String(x.threshold, 1)
                  << "  " << x.detail << "\n";
    // Every caption under the 4.5:1 floor, worst first. R6's single worst value
    // says nothing about whether one caption is wrong or forty are, and a 6b pass
    // needs the list rather than the maximum.
    {
        struct Item { double ratio; juce::String what; };
        std::vector<Item> under;
        for (const auto& t : sc.texts)
        {
            if (t.text.trim().isEmpty()) continue;
            const auto r = t.bounds.getIntersection(sc.canvas);
            const auto gb = fvvis::glyphBox(t).getIntersection(sc.canvas);
            if (r.getWidth() < 3 || r.getHeight() < 3 || gb.getWidth() < 2 || gb.getHeight() < 2) continue;
            const auto bg = fvvis::modalColour(sc, r);
            juce::Colour fg;
            const int minPixels = juce::jmax(3, (gb.getWidth() * gb.getHeight()) / 100);
            if (!fvvis::glyphColour(sc, gb, bg, minPixels, fg)) continue;
            const double ratio = fvvis::contrastRatio(fg, bg);
            if (ratio < 4.5)
                under.push_back({ ratio, t.owner + " \"" + t.text + "\" " + juce::String(ratio, 2)
                                  + ":1 (" + fg.toDisplayString(false) + " on " + bg.toDisplayString(false)
                                  + ", " + juce::String(t.font.getHeight(), 1) + " px)" });
        }
        std::sort(under.begin(), under.end(), [](const Item& a, const Item& b) { return a.ratio < b.ratio; });
        std::cout << "  R6: " << under.size() << " captions under 4.5:1";
        for (size_t i = 0; i < under.size() && i < 10; ++i)
            std::cout << "\n        " << under[i].what;
        std::cout << "\n";
    }
    std::cout << "  R2 exempted " << dimensionlessExempted
              << " dimensionless readouts (Q, RATIO, BAL)\n";
    // drawFitted() condenses a readout that does not fit instead of clipping it, and
    // bottoms out at 0.55 - at the floor the text IS clipped. R3 is measured on the
    // condensed font, so this is the number that says how close the layout is to the
    // floor.
    std::cout << "  worst horizontal condense " << juce::String(worstCondense, 3)
              << " (floor 0.55) on "
              << (worstCondenseWhat.isEmpty() ? juce::String("nothing - no readout needed condensing")
                                              : worstCondenseWhat) << "\n";
    juce::Rectangle<int> wide;
    const auto loose = fvvis::largestEmptyRect(sc, 14, &wide);
    const double area = (double)sc.canvas.getWidth() * sc.canvas.getHeight();
    std::cout << "  R12 again at tolerance 14/255: " << juce::String(100.0 * (double)loose / area, 2)
              << " % (" << wide.getWidth() << "x" << wide.getHeight() << " at "
              << wide.getX() << "," << wide.getY() << ")\n";
}

// Drives every automatable parameter to one end of its range, or back to default.
enum class ParamState { Defaults, Minimum, Maximum };

void setAllParameters(MixAgentAudioProcessor& proc, ParamState st)
{
    for (auto* p : proc.getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*>(p))
        {
            const float v = st == ParamState::Defaults ? rp->getDefaultValue()
                          : st == ParamState::Minimum  ? 0.0f : 1.0f;
            rp->setValueNotifyingHost(v);
        }
}

float rawValue(juce::AudioProcessorValueTreeState& apvts, const juce::String& id)
{
    auto* p = dynamic_cast<juce::RangedAudioParameter*>(apvts.getParameter(id));
    return p != nullptr ? p->getNormalisableRange().convertFrom0to1(p->getValue()) : 0.0f;
}
} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    {
        MixAgentAudioProcessor proc;
        proc.prepareToPlay(44100.0, 512);
        auto ed = std::unique_ptr<juce::AudioProcessorEditor>(proc.createEditor());

        juce::DocumentWindow dw("probe", juce::Colours::black,
                                juce::DocumentWindow::allButtons, false);
        dw.setContentOwned(ed.release(), false);
        dw.setUsingNativeTitleBar(false);
        dw.setBounds(0, 0, 1160, 920);
        dw.setVisible(true);

        auto* edPtr = dw.getContentComponent();
        if (edPtr == nullptr)
        {
            std::cout << "NO CONTENT\n";
            return 1;
        }

        edPtr->setBounds(0, 0, 1160, 920);
        edPtr->setBounds(0, 0, 900, 700);
        edPtr->setBounds(0, 0, 1160, 920);
        juce::Image img = edPtr->createComponentSnapshot(juce::Rectangle<int>(0, 0, 1160, 920));
        juce::File out = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("mixagent_ui_probe.png");
        {
            juce::FileOutputStream fos(out);
            juce::PNGImageFormat pf;
            pf.writeImageToStream(img, fos);
        }
        std::cout << "RENDERED " << out.getFullPathName() << " (" << img.getWidth() << "x" << img.getHeight() << ")\n";
        check(img.getWidth() == 1160 && img.getHeight() == 920, "editor renders a 1160x920 snapshot after resizes");

        auto& apvts = proc.getAPVTS();
        auto pump = [] { juce::MessageManager::getInstance()->runDispatchLoopUntil(60); };

        // Opening the editor must not touch a single parameter. ComboBox
        // selections notify asynchronously by default, so a combo the editor
        // initialises for itself can drive the processor a few milliseconds after
        // the window appears - which is how the preset ComboBox came to call
        // setCurrentProgram(0) and reset everything the moment the UI opened.
        {
            std::vector<juce::RangedAudioParameter*> ps;
            for (auto* p : proc.getParameters())
                if (auto* rp = dynamic_cast<juce::RangedAudioParameter*>(p)) ps.push_back(rp);
            std::vector<float> before;
            for (auto* rp : ps) before.push_back(rp->getValue());
            pump(); pump(); pump();
            int moved = 0;
            for (size_t i = 0; i < ps.size(); ++i)
                if (std::abs(ps[i]->getValue() - before[i]) > 1e-6f)
                {
                    std::cout << "    " << ps[i]->paramID << " moved on its own\n";
                    ++moved;
                }
            check(moved == 0, "opening the editor and letting the message loop run changes no parameter");
        }

        // ---- program ComboBox -> inst_program
        std::vector<juce::ComboBox*> combos;
        collect<juce::ComboBox>(*edPtr, combos);
        juce::ComboBox* programCombo = nullptr;
        for (auto* c : combos)
            for (int i = 0; i < c->getNumItems(); ++i)
                if (c->getItemText(i) == agm::InstrumentBank::programName((int)agm::InstrumentBank::Organ))
                    programCombo = c;
        check(programCombo != nullptr, "program ComboBox found");
        if (programCombo != nullptr)
        {
            const int organId = (int)agm::InstrumentBank::Organ + 1;
            programCombo->setSelectedId(organId, juce::sendNotificationSync);
            pump();
            check((int)rawValue(apvts, "inst_program") == (int)agm::InstrumentBank::Organ
                      && proc.getInstrumentProgram() == (int)agm::InstrumentBank::Organ,
                  "selecting 'Organ' in the ComboBox sets inst_program and the bank program");
            // and the other way round: parameter -> ComboBox
            if (auto* p = dynamic_cast<juce::RangedAudioParameter*>(apvts.getParameter("inst_program")))
                p->setValueNotifyingHost(p->getNormalisableRange().convertTo0to1((float)agm::InstrumentBank::Bell));
            pump();
            check(programCombo->getSelectedId() == (int)agm::InstrumentBank::Bell + 1, "inst_program parameter change updates the ComboBox");
        }

        // ---- knobs (Sliders) -> parameters: move every slider to its maximum in turn;
        // each must move exactly one parameter, to that parameter's maximum
        std::vector<juce::Slider*> sliders;
        collect<juce::Slider>(*edPtr, sliders);
        check(!sliders.empty(), "knobs found");
        std::vector<juce::RangedAudioParameter*> params;
        for (auto* p : proc.getParameters())
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*>(p)) params.push_back(rp);
        int mapped = 0, broken = 0;
        std::vector<juce::RangedAudioParameter*> owner(sliders.size(), nullptr);
        size_t sliderIdx = 0;
        for (auto* s : sliders)
        {
            // park at the minimum first: several parameters (mix, width) default to their maximum
            s->setValue(s->getMinimum(), juce::sendNotificationSync);
            pump();
            std::vector<float> before;
            for (auto* rp : params) before.push_back(rp->getValue());
            s->setValue(s->getMaximum(), juce::sendNotificationSync);
            pump();
            int changed = 0; bool atMax = false;
            for (size_t i = 0; i < params.size(); ++i)
                if (std::abs(params[i]->getValue() - before[i]) > 1e-6f)
                {
                    ++changed;
                    atMax = std::abs(params[i]->getValue() - 1.0f) < 1e-4f;
                    owner[sliderIdx] = params[i];
                }
            if (changed == 1 && atMax) ++mapped;
            else if (changed != 0) { broken++; owner[sliderIdx] = nullptr; }
            ++sliderIdx;
        }
        std::cout << "  sliders: " << sliders.size() << ", mapped 1:1 to a parameter: " << mapped << ", ambiguous: " << broken << "\n";
        check(mapped >= 1 && broken == 0, "every knob that moves a parameter moves exactly that one parameter to its maximum");
        check(mapped == (int)sliders.size(), "every knob on the editor is attached to a parameter");

        // Double-click-to-default must land on the parameter's real default, not
        // on its normalised one. Match each slider to its parameter by range.
        {
            int wrong = 0, checkedKnobs = 0;
            for (size_t i = 0; i < sliders.size(); ++i)
            {
                auto* rp = owner[i];
                if (rp == nullptr) continue;
                const auto& r = rp->getNormalisableRange();
                const double want = (double)r.convertFrom0to1(rp->getDefaultValue());
                if (std::abs(sliders[i]->getDoubleClickReturnValue() - want) > 1e-4 * std::max(1.0, std::abs(want)))
                {
                    if (wrong < 6)
                        std::cout << "    " << rp->paramID << ": double-click goes to "
                                  << sliders[i]->getDoubleClickReturnValue() << ", default is " << want << "\n";
                    ++wrong;
                }
                ++checkedKnobs;
            }
            std::cout << "  knobs whose double-click-to-default is wrong: " << wrong
                      << " of " << checkedKnobs << "\n";
            check(wrong == 0, "double-clicking a knob returns it to its parameter's real default");
        }

        // Every automatable float parameter should be reachable from the editor.
        {
            int missing = 0;
            for (auto* rp : params)
            {
                if (dynamic_cast<juce::AudioParameterBool*>(rp) != nullptr
                    || dynamic_cast<juce::AudioParameterChoice*>(rp) != nullptr)
                    continue;
                bool found = false;
                for (auto* o : owner) if (o == rp) { found = true; break; }
                if (!found) { std::cout << "    no control for " << rp->paramID << "\n"; ++missing; }
            }
            check(missing == 0, "every continuous parameter has a control on the editor");
        }

        // Readouts must actually say something. Ten controls with a 0..1 range
        // were drawn with zero decimal places, so they could only ever print "0"
        // or "1"; the frequency knobs printed a bare number with no unit.
        {
            std::vector<agm::ui::Knob*> knobs;
            collect<agm::ui::Knob>(*edPtr, knobs);
            int flat = 0;
            for (auto* k : knobs)
            {
                const double lo = k->getMinimum(), hi = k->getMaximum();
                juce::String seen[3];
                for (int i = 0; i < 3; ++i)
                {
                    k->setValue(lo + (hi - lo) * (0.25 + 0.25 * i), juce::sendNotificationSync);
                    seen[i] = k->readout();
                }
                if (seen[0] == seen[1] || seen[1] == seen[2])
                {
                    std::cout << "    readout does not change: \"" << seen[0] << "\" / \""
                              << seen[1] << "\" / \"" << seen[2] << "\"\n";
                    ++flat;
                }
            }
            std::cout << "  knobs: " << knobs.size() << ", readouts that do not change across the range: "
                      << flat << "\n";
            check(flat == 0, "every knob's readout changes at 25/50/75 % of its range");
        }

        // ---- toggles -> bool parameters
        std::vector<juce::ToggleButton*> toggles;
        collect<juce::ToggleButton>(*edPtr, toggles);
        int toggleMapped = 0;
        for (auto* t : toggles)
        {
            std::vector<float> before;
            for (auto* rp : params) before.push_back(rp->getValue());
            t->setToggleState(!t->getToggleState(), juce::sendNotificationSync);
            pump();
            int changed = 0;
            for (size_t i = 0; i < params.size(); ++i)
                if (std::abs(params[i]->getValue() - before[i]) > 1e-6f) ++changed;
            if (changed == 1) ++toggleMapped;
            t->setToggleState(!t->getToggleState(), juce::sendNotificationSync);
            pump();
        }
        std::cout << "  toggles: " << toggles.size() << ", attached to a parameter: " << toggleMapped << " (FAV is state, not a parameter)\n";
        check(toggleMapped >= (int)toggles.size() - 1, "all power/option toggles except FAV drive a parameter");

        // ------------------------------------------------------------------
        // Wave 6a: what the editor LOOKS like. Measured at three parameter
        // states, because the readouts are what change width here, not the
        // window.
        auto* mix = dynamic_cast<MixAgentAudioProcessorEditor*>(edPtr);
        check(mix != nullptr, "the editor is a MixAgentAudioProcessorEditor and can be measured");
        if (mix != nullptr)
        {
            const char* shotDir = std::getenv("AGM_SHOT_DIR");
            struct Case { const char* name; ParamState st; };
            const Case cases[3] = { { "defaults", ParamState::Defaults },
                                    { "all-minimum", ParamState::Minimum },
                                    { "all-maximum", ParamState::Maximum } };
            for (const auto& cs : cases)
            {
                setAllParameters(proc, cs.st);
                pump();
                mix->setBounds(0, 0, 1160, 920);
                fvvis::Scene sc;
                int exempted = 0;
                float condense = 1.0f;
                juce::String condenseWhat;
                buildScene(*mix, sc, cs.name, exempted, condense, condenseWhat);
                const auto f = fvvis::measure(sc);
                reportScene(sc, f, exempted, condense, condenseWhat);
                if (shotDir != nullptr)
                {
                    const auto bytes = sc.captureTo(juce::String(shotDir), sc.name);
                    std::cout << "  wrote " << bytes << " bytes of PNG for " << sc.name << "\n";
                    check(bytes > 0, vmsg(juce::String("PNG + component tree written for ") + cs.name));
                }
                auto rule = [&](const char* id, const char* what)
                {
                    const auto* x = fvvis::find(f, id);
                    check(x != nullptr && x->pass,
                          vmsg(juce::String(cs.name) + ": " + id + " - " + what
                               + (x != nullptr && x->detail.isNotEmpty()
                                  ? juce::String(" [") + x->detail + "]" : juce::String())));
                };
                // Eight rules this editor now meets, asserted at the rubric's own
                // threshold.
                rule("R1",  "no readout prints a raw float");
                rule("R2",  "every numeric readout field carries a unit, consistent per panel");
                rule("R3",  "no text is clipped by its own bounds");
                rule("R4",  "every component is inside its parent and inside the window");
                rule("R5",  "no two sibling text/interactive components overlap");
                rule("R7",  "every painted indicator (state, value, scale) is at least 3:1");
                rule("R8",  "every interactive component is at least 24x24 px");
                rule("R12", "the largest empty rectangle is at most 15 % of the canvas");

                // Five rules this editor does NOT meet. Closing any of them means
                // changing the visual language - a bigger type scale, a smaller
                // palette, a re-derived coordinate system, a meter wide enough to
                // label - and this wave is defects only, so they are measured,
                // printed, written into AUDIT.md as open items, and RATCHETED: the
                // assertion is "no worse than the value recorded today", so a later
                // pass cannot quietly give ground. The real threshold is in the
                // message beside it.
                auto ratchet = [&](const char* id, bool lowerIsBetter, double recorded,
                                   const char* what)
                {
                    const auto* x = fvvis::find(f, id);
                    const double m = x != nullptr ? x->measured : -1.0;
                    const bool ok = x != nullptr
                                 && (lowerIsBetter ? m <= recorded : m >= recorded);
                    check(ok, vmsg(juce::String(cs.name) + ": " + id
                                   + " is OPEN and ratcheted - " + what + ", measured "
                                   + juce::String(m, 2) + (lowerIsBetter ? " <= " : " >= ")
                                   + juce::String(recorded, 2) + " recorded"));
                };
                ratchet("R6",  false, 2.80, "44 captions under 4.5:1, worst 2.95:1; 8.5 px type "
                                            "cannot reach 4.5:1 rendered in any dimmed colour "
                                            "(threshold 4.5:1)");
                ratchet("R9",  true,  9.0,  "nine font sizes, six of them between 8.5 and 11 px "
                                            "(threshold 5)");
                ratchet("R10", true,  7.0,  "seven hues: orange accent, amber, cyan, pad blue and "
                                            "three meter colours (threshold 6)");
                ratchet("R11", true,  76.0, "all 76 laid-out components off the 4 px grid "
                                            "(threshold 0)");
                // R11's COUNT is saturated - every laid-out component is already off the
                // grid - so the ratchet on it cannot detect anything getting worse. The
                // total off-grid RESIDUAL can: it is the sum over components of
                // (x%4 + y%4 + w%4 + h%4), which moves whenever any bound moves. This is
                // the quantity a grid mutation is actually measured against.
                {
                    int residual = 0, placed = 0;
                    for (const auto& n : sc.nodes)
                    {
                        if (n.parent < 0 || !n.visible) continue;
                        ++placed;
                        residual += n.bounds.getX() % 4 + n.bounds.getY() % 4
                                  + n.bounds.getWidth() % 4 + n.bounds.getHeight() % 4;
                    }
                    std::cout << "  R11 residual " << residual << " over " << placed
                              << " placed components (0 would be a perfect 4 px grid)\n";
                    check(residual <= 385,
                          vmsg(juce::String(cs.name) + ": R11 off-grid residual is pinned at its "
                               "recorded value, measured " + juce::String(residual) + " <= 385"));
                }
                ratchet("R13", true,  4.0,  "four meters, none with a labelled scale or a reference "
                                            "mark (threshold 0)");
            }
            setAllParameters(proc, ParamState::Defaults);
            pump();
        }
    }
    std::cout << (failures == 0 ? "EDITOR TEST OK" : "EDITOR TEST FAILED") << "\n";
    return failures == 0 ? 0 : 1;
}
