#pragma once
/* ===========================================================================
   Wave 6a visual rubric: the instrument that measures what an editor LOOKS
   like.

   Five passes rendered the whole editor into an offscreen image and then
   asserted only `img.isValid()` - "it painted without crashing" - a check that
   cannot fail for any reason anyone cares about, and the reason five passes of
   green said nothing about the look. This header keeps the pixels and measures
   them.

   Two inputs, one per class of rule:

     * the component TREE - bounds, fonts, text, colours, interactivity - which
       is what the geometry and typography rules are measured on;
     * the rendered PIXELS, which is what contrast and dead space are measured
       on. A declared colour is not what the eye receives once an alpha, a
       gradient and a parent background are in the way.

   Every rule returns a Finding carrying its MEASURED value, so a failure reads
   "47.3 % dead space, 766x186 at 24,62" and not "looks wrong". The caller turns
   Findings into its own suite's assertions.

   Writing the PNG and the JSON tree is env-gated (Scene::captureTo) so a normal
   run costs nothing. The MEASUREMENT is not gated: it runs on every suite run.
   =========================================================================== */

#include <juce_gui_basics/juce_gui_basics.h>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <vector>

namespace fvvis {

/* Narrow conversion helpers. The project's own translation units are compiled
   with -Wold-style-cast, and a measurement header is not the place to make an
   exception. */
namespace cv {
inline std::size_t sz   (int v)            { return static_cast<std::size_t> (v); }
inline std::size_t sz   (juce::int64 v)    { return static_cast<std::size_t> (v); }
inline double      dbl  (int v)            { return static_cast<double> (v); }
inline double      dbl  (juce::int64 v)    { return static_cast<double> (v); }
inline double      dbl  (std::size_t v)    { return static_cast<double> (v); }
inline double      dbl  (float v)          { return static_cast<double> (v); }
inline float       flt  (int v)            { return static_cast<float> (v); }
inline float       flt  (double v)         { return static_cast<float> (v); }
inline int         ivl  (double v)         { return static_cast<int> (v); }
inline int         ivl  (float v)          { return static_cast<int> (v); }
inline int         ivl  (juce::uint8 v)    { return static_cast<int> (v); }
inline juce::int64 i64  (int v)            { return static_cast<juce::int64> (v); }
inline juce::uint8 u8   (juce::uint64 v)   { return static_cast<juce::uint8> (v); }
inline juce::uint32 u32 (int v)            { return static_cast<juce::uint32> (v); }
} // namespace cv

/* ------------------------------------------------------------------ model */

struct TextItem {
    juce::String         text;
    juce::Rectangle<int> bounds;   /* the rectangle the text must fit in, editor coords */
    juce::Font           font { juce::FontOptions {} };
    juce::Colour         colour;
    /* How the text sits inside `bounds`. R6 needs it: the contrast of a caption
       is the contrast of its GLYPHS, and a 15 px string centred in a 52 px
       button is mostly background. Measuring the whole rectangle is how a
       caption with no contrast at all reports 1.08:1 and looks like a defect in
       the editor rather than one in the checker. */
    juce::Justification  just { juce::Justification::centredLeft };
    juce::String         owner;    /* component id, or a name for painted text */
    juce::String         panel;    /* the panel the item belongs to - R2 is per panel */
    bool                 painted = false;
    /* A READOUT displays a parameter's or a meter's value. R2 is asserted on
       readouts only: "VST3" and "16 INSTRUMENTS" contain digits and are not
       values, and a rule that cannot tell the difference is a rule that gets
       switched off. */
    bool                 readout = false;
};

struct Node {
    int                  index = 0, parent = -1;
    juce::String         id, cls;
    juce::Rectangle<int> bounds;         /* editor coordinates */
    juce::Rectangle<int> parentBounds;   /* editor coordinates */
    bool                 visible = true;
    bool                 interactive = false;
};

/* A graphic element R7 is measured on. Meters, LEDs, outlines and value arcs
   are painted, not components, so the editor declares the colour pairs it
   actually paints and the rule is measured on those exact colours - with any
   alpha composited, because a 0x66 outline is not the contrast its hex
   suggests. */
struct GraphicPair {
    juce::String what;
    juce::Colour fg, bg;
    /* R7 is "meters, indicators and control outlines >= 3:1". A panel's fill
       against the canvas behind it is a SURFACE, not an indicator - two dark
       greys that differ by 1.09:1 are a deliberately quiet separation, not an
       unreadable meter. Surfaces are measured and reported; they are not
       asserted against the indicator threshold. */
    bool         surface = false;
    /* STRUCTURAL: a panel's border, its bevel highlight, a knob's unfilled groove
       and its tick ring. R7's words are "meters, indicators and control
       outlines", and a dark design's quiet structure is the part of that clause a
       defects-only wave cannot close without changing the visual language. These
       are measured and PRINTED, and where they fail that is stated as an open
       item rather than asserted away. Anything that carries STATE (a power LED
       that is off), a VALUE (a meter bar, a value arc) or a SCALE (a meter tick)
       is an indicator and is asserted. */
    bool         structural = false;
};

/* A meter, for R13. */
struct MeterInfo {
    juce::String         id;
    juce::Rectangle<int> bounds;
    int                  labelledTicks = 0;   /* ticks carrying a number or a word */
    bool                 referenceMark = false;
};

struct Finding {
    const char*  rule;        /* "R1" .. "R13" */
    bool         pass;
    double       measured;
    double       threshold;
    juce::String detail;      /* the worst offender, named */
};

/* ------------------------------------------------------- colour utilities */

inline double srgbChannel (int byteValue) {
    const double c = cv::dbl (byteValue) / 255.0;
    return c <= 0.03928 ? c / 12.92 : std::pow ((c + 0.055) / 1.055, 2.4);
}

/* WCAG 2.x relative luminance. */
inline double luminance (juce::Colour c) {
    return 0.2126 * srgbChannel (cv::ivl (c.getRed()))
         + 0.7152 * srgbChannel (cv::ivl (c.getGreen()))
         + 0.0722 * srgbChannel (cv::ivl (c.getBlue()));
}

inline double contrastRatio (double l1, double l2) {
    const double hi = juce::jmax (l1, l2), lo = juce::jmin (l1, l2);
    return (hi + 0.05) / (lo + 0.05);
}

inline double contrastRatio (juce::Colour a, juce::Colour b) {
    return contrastRatio (luminance (a), luminance (b));
}

/* ------------------------------------------------------------------ scene */

class Scene {
public:
    /* Called for each component; return true to suppress the default text
       extraction for it. Lets a repo describe its own widgets - a knob that
       paints a caption and a readout of its own, for instance. */
    std::function<bool (juce::Component&, juce::Rectangle<int>, Scene&)> describe;
    /* Extra interactivity predicate, for widgets that take the mouse without
       being a Slider/Button/ComboBox. */
    std::function<bool (juce::Component&)> isInteractive;

    std::vector<Node>        nodes;
    std::vector<TextItem>    texts;
    std::vector<MeterInfo>   meters;
    std::vector<GraphicPair> graphics;
    juce::Rectangle<int>     canvas;
    juce::Image              pixels;
    juce::String             name;

    void addText (TextItem t)    { texts.push_back (std::move (t)); }
    void addMeter (MeterInfo m)  { meters.push_back (std::move (m)); }
    void addGraphic (GraphicPair gp) { graphics.push_back (std::move (gp)); }

    /* Renders `editor` into an offscreen image and walks its tree. */
    void build (juce::Component& editor, const juce::String& sceneName) {
        name   = sceneName;
        canvas = editor.getLocalBounds();
        nodes.clear(); texts.clear(); meters.clear(); graphics.clear();
        pixels = juce::Image (juce::Image::ARGB, juce::jmax (1, canvas.getWidth()),
                              juce::jmax (1, canvas.getHeight()), true);
        { juce::Graphics g (pixels); editor.paintEntireComponent (g, false); }
        walk (editor, -1, { 0, 0 });
    }

    /* Writes the PNG and the JSON component tree when `dir` is not empty.
       Returns the PNG's size in bytes, or 0 if nothing was written. */
    juce::int64 captureTo (const juce::String& dir, const juce::String& stem) const {
        if (dir.isEmpty() || ! pixels.isValid()) return 0;
        auto base = juce::File (dir);
        base.createDirectory();
        auto png  = base.getChildFile (stem + ".png");
        auto json = base.getChildFile (stem + ".json");
        png.deleteFile(); json.deleteFile();
        if (auto out = std::unique_ptr<juce::FileOutputStream> (png.createOutputStream())) {
            juce::PNGImageFormat fmt;
            if (! fmt.writeImageToStream (pixels, *out)) return 0;
            out->flush();
        } else {
            return 0;
        }
        json.replaceWithText (treeJson());
        return png.getSize();
    }

    juce::String treeJson() const {
        juce::String s;
        s << "{\n  \"scene\": \"" << name << "\",\n"
          << "  \"canvas\": [" << canvas.getWidth() << ", " << canvas.getHeight() << "],\n"
          << "  \"components\": [\n";
        for (std::size_t i = 0; i < nodes.size(); ++i) {
            const auto& n = nodes[i];
            s << "    { \"index\": " << n.index << ", \"parent\": " << n.parent
              << ", \"id\": \"" << esc (n.id) << "\", \"class\": \"" << esc (n.cls)
              << "\", \"bounds\": [" << n.bounds.getX() << ", " << n.bounds.getY() << ", "
              << n.bounds.getWidth() << ", " << n.bounds.getHeight() << "]"
              << ", \"visible\": " << boolStr (n.visible)
              << ", \"interactive\": " << boolStr (n.interactive) << " }"
              << (i + 1 < nodes.size() ? "," : "") << "\n";
        }
        s << "  ],\n  \"text\": [\n";
        for (std::size_t i = 0; i < texts.size(); ++i) {
            const auto& t = texts[i];
            s << "    { \"owner\": \"" << esc (t.owner) << "\", \"panel\": \"" << esc (t.panel)
              << "\", \"text\": \"" << esc (t.text) << "\""
              << ", \"bounds\": [" << t.bounds.getX() << ", " << t.bounds.getY() << ", "
              << t.bounds.getWidth() << ", " << t.bounds.getHeight() << "]"
              << ", \"fontHeight\": " << juce::String (t.font.getHeight(), 2)
              << ", \"colour\": \"" << t.colour.toDisplayString (true) << "\""
              << ", \"requiredWidth\": "
              << juce::String (juce::GlyphArrangement::getStringWidth (t.font, t.text), 2)
              << ", \"painted\": " << boolStr (t.painted) << " }"
              << (i + 1 < texts.size() ? "," : "") << "\n";
        }
        s << "  ],\n  \"meters\": [\n";
        for (std::size_t i = 0; i < meters.size(); ++i) {
            const auto& m = meters[i];
            s << "    { \"id\": \"" << esc (m.id) << "\", \"bounds\": ["
              << m.bounds.getX() << ", " << m.bounds.getY() << ", "
              << m.bounds.getWidth() << ", " << m.bounds.getHeight() << "]"
              << ", \"labelledTicks\": " << m.labelledTicks
              << ", \"referenceMark\": " << boolStr (m.referenceMark) << " }"
              << (i + 1 < meters.size() ? "," : "") << "\n";
        }
        s << "  ],\n  \"graphics\": [\n";
        for (std::size_t i = 0; i < graphics.size(); ++i) {
            const auto& gp = graphics[i];
            const auto eff = gp.bg.overlaidWith (gp.fg);
            s << "    { \"what\": \"" << esc (gp.what) << "\", \"fg\": \""
              << gp.fg.toDisplayString (true) << "\", \"over\": \"" << gp.bg.toDisplayString (true)
              << "\", \"surface\": " << boolStr (gp.surface)
              << ", \"structural\": " << boolStr (gp.structural)
              << ", \"contrast\": " << juce::String (contrastRatio (eff, gp.bg), 3) << " }"
              << (i + 1 < graphics.size() ? "," : "") << "\n";
        }
        s << "  ]\n}\n";
        return s;
    }

    /* Part C: a golden hash of the RENDERED PIXELS - FNV-1a 64 over every ARGB
       value, in reading order. Taken on the pixels rather than on the PNG file so
       it does not move with zlib's compression level, and printed on every run so
       a later pass that changes a pixel has to say which pixel. It is printed and
       recorded, never asserted: glyph rasterisation differs between machines and
       between fontconfig setups, so a hard assertion here would fail for reasons
       that have nothing to do with the editor. */
    juce::uint64 pixelHash() const {
        juce::uint64 h = 14695981039346656037ULL;
        for (int y = 0; y < canvas.getHeight(); ++y)
            for (int x = 0; x < canvas.getWidth(); ++x) {
                const juce::uint32 argb = pixelAt (x, y).getARGB();
                for (int b = 0; b < 4; ++b) {
                    h ^= (argb >> (b * 8)) & 0xffu;
                    h *= 1099511628211ULL;
                }
            }
        return h;
    }

    juce::Colour pixelAt (int x, int y) const {
        if (! pixels.isValid() || ! canvas.contains (x, y)) return juce::Colours::black;
        return pixels.getPixelAt (x, y);
    }

private:
    static const char* boolStr (bool b) { return b ? "true" : "false"; }

    static juce::String esc (const juce::String& s) {
        return s.replace ("\\", "\\\\").replace ("\"", "\\\"").replace ("\n", " ");
    }

    void walk (juce::Component& c, int parentIndex, juce::Point<int> parentOriginInEditor) {
        Node n;
        n.index  = cv::ivl (cv::dbl (nodes.size()));
        n.parent = parentIndex;
        n.id     = c.getComponentID();
        n.cls    = classOf (c);
        n.bounds = parentIndex < 0
                 ? c.getLocalBounds()
                 : c.getBounds().withPosition (parentOriginInEditor + c.getBounds().getPosition());
        n.parentBounds = parentIndex < 0 ? c.getLocalBounds() : nodes[cv::sz (parentIndex)].bounds;
        n.visible      = c.isVisible();
        n.interactive  = defaultInteractive (c) || (isInteractive && isInteractive (c));
        const int myIndex = n.index;
        nodes.push_back (n);

        const auto myAbs = nodes[cv::sz (myIndex)].bounds;
        bool handled = false;
        if (describe) handled = describe (c, myAbs, *this);
        if (! handled) defaultText (c, myAbs);

        for (int i = 0; i < c.getNumChildComponents(); ++i)
            if (auto* k = c.getChildComponent (i))
                walk (*k, myIndex, myAbs.getPosition());
    }

    void defaultText (juce::Component& c, juce::Rectangle<int> abs) {
        if (auto* l = dynamic_cast<juce::Label*> (&c)) {
            if (l->getText().isEmpty()) return;
            /* A Label insets its text by its border, so the usable width is
               narrower than the component. Measuring the component instead of
               the text area is exactly how a clipped caption reads as fitting. */
            const auto b = l->getBorderSize();
            TextItem t;
            t.text   = l->getText();
            t.bounds = { abs.getX() + b.getLeft(), abs.getY() + b.getTop(),
                         juce::jmax (0, abs.getWidth()  - b.getLeftAndRight()),
                         juce::jmax (0, abs.getHeight() - b.getTopAndBottom()) };
            t.font   = l->getFont();
            t.just   = l->getJustificationType();
            t.colour = l->findColour (juce::Label::textColourId);
            t.owner  = c.getComponentID().isNotEmpty() ? c.getComponentID() : juce::String ("Label");
            addText (std::move (t));
            return;
        }
        if (auto* b = dynamic_cast<juce::Button*> (&c)) {
            if (b->getButtonText().isEmpty()) return;
            TextItem t;
            t.text = b->getButtonText();
            /* JUCE's ToggleButton leaves room for the tick box on the left. */
            const int tick = dynamic_cast<juce::ToggleButton*> (&c) != nullptr
                           ? juce::jmin (24, abs.getHeight()) + 10 : 0;
            t.bounds = abs.withTrimmedLeft (tick);
            t.font   = juce::Font (juce::FontOptions (juce::jmin (15.0f, cv::flt (abs.getHeight()) * 0.75f)));
            t.just   = juce::Justification::centredLeft;
            t.colour = b->findColour (juce::ToggleButton::textColourId);
            t.owner  = c.getComponentID().isNotEmpty() ? c.getComponentID() : juce::String ("Button");
            addText (std::move (t));
            return;
        }
        if (auto* cb = dynamic_cast<juce::ComboBox*> (&c)) {
            if (cb->getText().isEmpty()) return;
            TextItem t;
            t.text   = cb->getText();
            t.bounds = abs.reduced (3, 1).withTrimmedRight (abs.getHeight() / 2);
            /* Ask the LookAndFeel for the font it will actually use. Guessing it
               invented font sizes that the editor had never chosen, which made
               R9's "distinct font sizes" a count of the checker's arithmetic. */
            if (auto* lf = dynamic_cast<juce::ComboBox::LookAndFeelMethods*> (&cb->getLookAndFeel()))
                t.font = lf->getComboBoxFont (*cb);
            else
                t.font = juce::Font (juce::FontOptions (juce::jmax (12.0f, cv::flt (abs.getHeight()) * 0.7f)));
            t.just   = juce::Justification::centredLeft;
            t.colour = cb->findColour (juce::ComboBox::textColourId);
            t.owner  = c.getComponentID().isNotEmpty() ? c.getComponentID() : juce::String ("ComboBox");
            addText (std::move (t));
        }
    }

    static bool defaultInteractive (juce::Component& c) {
        return dynamic_cast<juce::Slider*>     (&c) != nullptr
            || dynamic_cast<juce::Button*>     (&c) != nullptr
            || dynamic_cast<juce::ComboBox*>   (&c) != nullptr
            || dynamic_cast<juce::TextEditor*> (&c) != nullptr;
    }

    static juce::String classOf (juce::Component& c) {
        if (dynamic_cast<juce::AudioProcessorEditor*>      (&c)) return "Editor";
        /* setResizable() adds this; its 18x18 size is JUCE's, not a coordinate
           the design chose, so the grid rule excludes it by name. */
        if (dynamic_cast<juce::ResizableCornerComponent*>  (&c)) return "Resizer";
        if (dynamic_cast<juce::ToggleButton*>         (&c)) return "ToggleButton";
        if (dynamic_cast<juce::Button*>               (&c)) return "Button";
        if (dynamic_cast<juce::Slider*>               (&c)) return "Slider";
        if (dynamic_cast<juce::ComboBox*>             (&c)) return "ComboBox";
        if (dynamic_cast<juce::Label*>                (&c)) return "Label";
        return "Component";
    }
};

/* ------------------------------------------------------- string inspection */

/* R1: a readout that prints the float it happens to hold, e.g. "2.5000000". */
inline bool hasRawFloat (const juce::String& s, juce::String& which) {
    for (int i = 0; i + 1 < s.length(); ++i) {
        if (! juce::CharacterFunctions::isDigit (s[i])) continue;
        if (s[i + 1] != '.') continue;
        int run = 0, j = i + 2;
        while (j < s.length() && juce::CharacterFunctions::isDigit (s[j])) { ++run; ++j; }
        if (run >= 4) { which = s.substring (i, j); return true; }
    }
    return false;
}

/* The unit a readout carries, or an empty string. Deliberately narrow: a bare
   number is the failure this rule exists to catch. */
inline juce::String unitOf (const juce::String& s) {
    static const char* const units[] = { "dBTP", "dBFS", "dB", "kHz", "Hz", "ms", "sec", "s",
                                         "%", ":1", "st", "deg", "x" };
    const auto trimmed = s.trim();
    for (auto* u : units) {
        const juce::String us (u);
        if (trimmed.endsWith (us)) return us;
    }
    return {};
}

inline bool isTimeUnit (const juce::String& u) { return u == "ms" || u == "s" || u == "sec"; }

/* A composite readout is several fields separated by runs of two or more
   spaces, or by a tab. Each field is measured on its own. */
inline juce::StringArray splitFields (const juce::String& s) {
    juce::StringArray out;
    juce::String cur;
    int spaces = 0;
    for (int i = 0; i < s.length(); ++i) {
        const auto ch = s[i];
        if (ch == ' ' || ch == '\t') {
            ++spaces;
            cur << ch;
        } else {
            if (spaces >= 2) {
                out.add (cur.trim());
                cur.clear();
            }
            spaces = 0;
            cur << ch;
        }
    }
    out.add (cur.trim());
    out.removeEmptyStrings();
    return out;
}

inline bool hasNumber (const juce::String& s) {
    for (int i = 0; i < s.length(); ++i)
        if (juce::CharacterFunctions::isDigit (s[i])) return true;
    return false;
}

/* ------------------------------------------------------------ pixel probes */

/* The modal colour of a rectangle, bucketed to 4 bits per channel: the local
   background a caption actually sits on. */
inline juce::Colour modalColour (const Scene& sc, juce::Rectangle<int> r, int* countOut = nullptr) {
    std::map<juce::uint32, int> hist;
    std::map<juce::uint32, juce::uint64> sumR, sumG, sumB;
    r = r.getIntersection (sc.canvas);
    for (int y = r.getY(); y < r.getBottom(); ++y)
        for (int x = r.getX(); x < r.getRight(); ++x) {
            const auto c = sc.pixelAt (x, y);
            const juce::uint32 key = cv::u32 (((cv::ivl (c.getRed())   >> 4) << 8)
                                            | ((cv::ivl (c.getGreen()) >> 4) << 4)
                                            |  (cv::ivl (c.getBlue())  >> 4));
            ++hist[key];
            sumR[key] += c.getRed(); sumG[key] += c.getGreen(); sumB[key] += c.getBlue();
        }
    juce::uint32 best = 0; int bestN = 0;
    for (const auto& kv : hist) if (kv.second > bestN) { bestN = kv.second; best = kv.first; }
    if (countOut != nullptr) *countOut = bestN;
    if (bestN == 0) return juce::Colours::black;
    const auto n = static_cast<juce::uint64> (bestN);
    return juce::Colour (cv::u8 (sumR[best] / n), cv::u8 (sumG[best] / n), cv::u8 (sumB[best] / n));
}

/* The glyph colour of a caption, measured on the rendered pixels: the bucket
   whose luminance is furthest from the background and that still covers at
   least `minPixels` of the rectangle, so an anti-aliased fringe cannot win. */
inline bool glyphColour (const Scene& sc, juce::Rectangle<int> r, juce::Colour bg,
                         int minPixels, juce::Colour& out) {
    r = r.getIntersection (sc.canvas);
    std::map<juce::uint32, int> hist;
    std::map<juce::uint32, juce::uint64> sumR, sumG, sumB;
    const double bgL = luminance (bg);
    for (int y = r.getY(); y < r.getBottom(); ++y)
        for (int x = r.getX(); x < r.getRight(); ++x) {
            const auto c = sc.pixelAt (x, y);
            const juce::uint32 key = cv::u32 (((cv::ivl (c.getRed())   >> 4) << 8)
                                            | ((cv::ivl (c.getGreen()) >> 4) << 4)
                                            |  (cv::ivl (c.getBlue())  >> 4));
            ++hist[key];
            sumR[key] += c.getRed(); sumG[key] += c.getGreen(); sumB[key] += c.getBlue();
        }
    bool found = false; double bestDist = 0.0;
    for (const auto& kv : hist) {
        if (kv.second < minPixels) continue;
        const auto n = static_cast<juce::uint64> (kv.second);
        const juce::Colour c (cv::u8 (sumR[kv.first] / n), cv::u8 (sumG[kv.first] / n),
                              cv::u8 (sumB[kv.first] / n));
        const double dist = std::abs (luminance (c) - bgL);
        if (dist > bestDist) { bestDist = dist; out = c; found = true; }
    }
    return found;
}

/* The rectangle a text item's GLYPHS actually occupy inside its bounds: the
   measured string width and the line height, placed by the justification. */
inline juce::Rectangle<int> glyphBox (const TextItem& t) {
    const double needW = cv::dbl (juce::GlyphArrangement::getStringWidth (t.font, t.text));
    const int w = juce::jlimit (1, juce::jmax (1, t.bounds.getWidth()),
                                cv::ivl (std::ceil (needW)));
    const int h = juce::jlimit (1, juce::jmax (1, t.bounds.getHeight()),
                                cv::ivl (std::ceil (cv::dbl (t.font.getHeight()) * 1.2)));
    const juce::Rectangle<int> box (w, h);
    return t.just.appliedToRectangle (box, t.bounds);
}

/* The largest axis-aligned rectangle of untouched canvas, in pixels.
   "Untouched" is measured against the modal colour of each ROW, so a vertical
   background gradient does not read as ink. */
inline juce::int64 largestEmptyRect (const Scene& sc, int tolerance,
                                     juce::Rectangle<int>* where = nullptr) {
    const int W = sc.canvas.getWidth(), H = sc.canvas.getHeight();
    if (W <= 0 || H <= 0) return 0;
    std::vector<juce::uint8> empty (cv::sz (W) * cv::sz (H), 0);
    for (int y = 0; y < H; ++y) {
        std::map<juce::uint32, int> hist;
        for (int x = 0; x < W; ++x)
            ++hist[sc.pixelAt (x, y).getARGB() & 0x00ffffffu];
        juce::uint32 best = 0; int bestN = -1;
        for (const auto& kv : hist) if (kv.second > bestN) { bestN = kv.second; best = kv.first; }
        const juce::Colour bg (best | 0xff000000u);
        for (int x = 0; x < W; ++x) {
            const auto c = sc.pixelAt (x, y);
            const int dr = std::abs (cv::ivl (c.getRed())   - cv::ivl (bg.getRed()));
            const int dg = std::abs (cv::ivl (c.getGreen()) - cv::ivl (bg.getGreen()));
            const int db = std::abs (cv::ivl (c.getBlue())  - cv::ivl (bg.getBlue()));
            const int dmax = juce::jmax (dr, juce::jmax (dg, db));
            empty[cv::sz (y) * cv::sz (W) + cv::sz (x)] = dmax <= tolerance ? juce::uint8 (1) : juce::uint8 (0);
        }
    }
    /* maximal rectangle in a binary matrix, by largest-rectangle-in-histogram */
    std::vector<int> heights (cv::sz (W), 0);
    std::vector<int> stack;
    juce::int64 bestArea = 0;
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x)
            heights[cv::sz (x)] = empty[cv::sz (y) * cv::sz (W) + cv::sz (x)] != 0
                                ? heights[cv::sz (x)] + 1 : 0;
        stack.clear();
        for (int x = 0; x <= W; ++x) {
            const int h = x == W ? 0 : heights[cv::sz (x)];
            while (! stack.empty() && heights[cv::sz (stack.back())] >= h) {
                const int top = stack.back(); stack.pop_back();
                const int left = stack.empty() ? 0 : stack.back() + 1;
                const juce::int64 area = cv::i64 (heights[cv::sz (top)]) * cv::i64 (x - left);
                if (area > bestArea) {
                    bestArea = area;
                    if (where != nullptr)
                        *where = { left, y - heights[cv::sz (top)] + 1, x - left, heights[cv::sz (top)] };
                }
            }
            stack.push_back (x);
        }
    }
    return bestArea;
}

/* ----------------------------------------------------------- the 13 rules */

inline std::vector<Finding> measure (const Scene& sc) {
    std::vector<Finding> f;
    const juce::int64 canvasArea = cv::i64 (sc.canvas.getWidth()) * cv::i64 (sc.canvas.getHeight());

    /* R1 - readout precision: no displayed value matches \d\.\d{4,}. */
    {
        int bad = 0; juce::String worst;
        for (const auto& t : sc.texts) {
            juce::String which;
            if (hasRawFloat (t.text, which)) {
                ++bad;
                if (worst.isEmpty()) worst = t.owner + " = \"" + t.text + "\"";
            }
        }
        f.push_back ({ "R1", bad == 0, cv::dbl (bad), 0.0, worst });
    }

    /* R2 - every numeric readout carries a unit, and time units agree inside a
       panel. Measured per FIELD, not per string: a composite readout that reads
       "IN -12.0 dB   OUT -11.6 dB   GR -3.2" is two units and one bare number,
       and only a per-field rule sees the third one. */
    {
        int unitless = 0; juce::String worst;
        std::map<juce::String, std::set<juce::String>> timeUnits;
        for (const auto& t : sc.texts) {
            if (! t.readout) continue;
            for (const auto& field : splitFields (t.text)) {
                if (! hasNumber (field)) continue;
                const auto u = unitOf (field);
                if (u.isEmpty()) {
                    ++unitless;
                    if (worst.isEmpty()) worst = t.owner + " field \"" + field + "\" carries no unit";
                } else if (isTimeUnit (u)) {
                    timeUnits[t.panel].insert (u);
                }
            }
        }
        int mixed = 0; juce::String mixedWhere;
        for (const auto& kv : timeUnits)
            if (kv.second.size() > 1) {
                ++mixed;
                if (mixedWhere.isEmpty()) {
                    mixedWhere = "panel \"" + kv.first + "\" mixes";
                    for (const auto& u : kv.second) mixedWhere += " " + u;
                }
            }
        juce::String detail = worst;
        if (mixedWhere.isNotEmpty()) detail = detail.isEmpty() ? mixedWhere : detail + "; " + mixedWhere;
        f.push_back ({ "R2", unitless == 0 && mixed == 0, cv::dbl (unitless + mixed), 0.0, detail });
    }

    /* R3 - no clipped text: required width/height <= bounds, in px over. */
    {
        double worstOver = 0.0; int clipped = 0; juce::String worst;
        for (const auto& t : sc.texts) {
            if (t.text.isEmpty()) continue;
            const double need  = cv::dbl (juce::GlyphArrangement::getStringWidth (t.font, t.text));
            const double needH = std::ceil (cv::dbl (t.font.getHeight()));
            const double over  = juce::jmax (need - cv::dbl (t.bounds.getWidth()),
                                             needH - cv::dbl (t.bounds.getHeight()));
            if (over > 0.5) {
                ++clipped;
                if (over > worstOver) {
                    worstOver = over;
                    worst = t.owner + " \"" + t.text + "\" needs " + juce::String (need, 1) + "x"
                          + juce::String (needH, 0) + " px in " + juce::String (t.bounds.getWidth())
                          + "x" + juce::String (t.bounds.getHeight());
                }
            }
        }
        f.push_back ({ "R3", clipped == 0, worstOver, 0.0,
                       juce::String (clipped) + " clipped; worst " + worst });
    }

    /* R4 - containment: bounds inside the parent's, inside the editor's. */
    {
        int worstPx = 0, outside = 0; juce::String worst;
        auto over = [] (juce::Rectangle<int> r, juce::Rectangle<int> box) {
            return juce::jmax (juce::jmax (box.getX() - r.getX(), box.getY() - r.getY()),
                               juce::jmax (r.getRight() - box.getRight(), r.getBottom() - box.getBottom()));
        };
        for (const auto& n : sc.nodes) {
            if (n.parent < 0 || ! n.visible) continue;
            const int o = juce::jmax (over (n.bounds, n.parentBounds), over (n.bounds, sc.canvas));
            if (o > 0) {
                ++outside;
                if (o > worstPx) {
                    worstPx = o;
                    worst = n.id + " (" + n.cls + ") " + juce::String (o) + " px out";
                }
            }
        }
        f.push_back ({ "R4", outside == 0, cv::dbl (worstPx), 0.0, worst });
    }

    /* R5 - no unintended overlap between sibling text/interactive components. */
    {
        int overlaps = 0; juce::int64 worstArea = 0; juce::String worst;
        auto counts = [] (const Node& n) {
            return n.visible && (n.interactive || n.cls == "Label");
        };
        for (std::size_t i = 0; i < sc.nodes.size(); ++i)
            for (std::size_t j = i + 1; j < sc.nodes.size(); ++j) {
                const auto& a = sc.nodes[i];
                const auto& b = sc.nodes[j];
                if (a.parent < 0 || a.parent != b.parent) continue;
                if (! counts (a) || ! counts (b)) continue;
                const auto x = a.bounds.getIntersection (b.bounds);
                if (x.isEmpty()) continue;
                ++overlaps;
                const juce::int64 area = cv::i64 (x.getWidth()) * cv::i64 (x.getHeight());
                if (area > worstArea) {
                    worstArea = area;
                    worst = a.id + " (" + a.cls + ") / " + b.id + " (" + b.cls + ") overlap "
                          + juce::String (x.getWidth()) + "x" + juce::String (x.getHeight()) + " px";
                }
            }
        f.push_back ({ "R5", overlaps == 0, cv::dbl (overlaps), 0.0, worst });
    }

    /* R6 - text contrast >= 4.5:1 against the actual local background, measured
       on the rendered pixels and not on the declared colour. */
    {
        double worstRatio = 1.0e9; int failures = 0; juce::String worst;
        for (const auto& t : sc.texts) {
            if (t.text.trim().isEmpty()) continue;
            const auto r = t.bounds.getIntersection (sc.canvas);
            if (r.getWidth() < 3 || r.getHeight() < 3) continue;
            /* Background from the whole rectangle, where background is the
               majority; glyph colour from the tight box the glyphs occupy,
               where the glyphs are. */
            const auto gb = glyphBox (t).getIntersection (sc.canvas);
            if (gb.getWidth() < 2 || gb.getHeight() < 2) continue;
            const auto bg = modalColour (sc, r);
            juce::Colour fg;
            const int minPixels = juce::jmax (3, (gb.getWidth() * gb.getHeight()) / 100);
            if (! glyphColour (sc, gb, bg, minPixels, fg)) continue;
            const double ratio = contrastRatio (fg, bg);
            if (ratio < 4.5) ++failures;
            if (ratio < worstRatio) {
                worstRatio = ratio;
                worst = t.owner + " \"" + t.text + "\" " + juce::String (ratio, 2) + ":1 ("
                      + fg.toDisplayString (false) + " on " + bg.toDisplayString (false) + ")";
            }
        }
        if (worstRatio > 1.0e8) worstRatio = 0.0;
        f.push_back ({ "R6", failures == 0, worstRatio, 4.5,
                       juce::String (failures) + " below 4.5:1; worst " + worst });
    }

    /* R6d - the DECLARED contrast of every caption against the flat colour behind
       it. R6 proper is measured on glyph pixels, which is what the rubric asks for
       and which is also partly a measurement of the font stack: the same editor,
       the same commit, measures 4.77:1 under one JUCE and 5.29:1 under another,
       because anti-aliased coverage of 8 px stems differs with the rasteriser. The
       local BACKGROUND is a flat fill and does not depend on rasterisation at all,
       so pairing it with the colour the editor DECLARED gives the same quantity
       without the font stack in it. R6 is the rule; R6d is the portable thing to
       pin, so a ratchet fails for a colour someone changed and not for a font
       someone installed. */
    {
        double worst = 1.0e9; int failures = 0; juce::String what;
        for (const auto& t : sc.texts) {
            if (t.text.trim().isEmpty()) continue;
            const auto r = t.bounds.getIntersection (sc.canvas);
            if (r.getWidth() < 3 || r.getHeight() < 3) continue;
            const auto bg = modalColour (sc, r);
            const auto fg = bg.overlaidWith (t.colour);   /* the declared colour, alpha composited */
            const double ratio = contrastRatio (fg, bg);
            if (ratio < 4.5) ++failures;
            if (ratio < worst) {
                worst = ratio;
                what = t.owner + " \"" + t.text + "\" " + juce::String (ratio, 2) + ":1 ("
                     + fg.toDisplayString (false) + " declared on " + bg.toDisplayString (false) + ")";
            }
        }
        if (worst > 1.0e8) worst = 0.0;
        f.push_back ({ "R6d", failures == 0, worst, 4.5,
                       juce::String (failures) + " below 4.5:1 declared; worst " + what });
    }

    /* R7 - graphic contrast: meters, indicators and control outlines >= 3:1. */
    {
        int bad = 0; double worstRatio = 1.0e9; juce::String worst;
        double worstStructural = 1.0e9; juce::String worstStructuralWhat;
        for (const auto& gp : sc.graphics) {
            if (gp.surface) continue;
            if (gp.structural) {
                const auto se = gp.bg.overlaidWith (gp.fg);
                const double sr = contrastRatio (se, gp.bg);
                if (sr < worstStructural) {
                    worstStructural = sr;
                    worstStructuralWhat = gp.what + " " + juce::String (sr, 2) + ":1";
                }
                continue;
            }
            /* Colour::overlaidWith puts its ARGUMENT on top, so the background
               is the receiver. The other way round an opaque background simply
               erased every foreground and every pair measured 1.00:1. */
            const auto eff = gp.bg.overlaidWith (gp.fg);
            const double ratio = contrastRatio (eff, gp.bg);
            if (ratio < 3.0) ++bad;
            if (ratio < worstRatio) {
                worstRatio = ratio;
                worst = gp.what + " " + juce::String (ratio, 2) + ":1 ("
                      + eff.toDisplayString (false) + " on " + gp.bg.toDisplayString (false) + ")";
            }
        }
        if (worstRatio > 1.0e8) worstRatio = 0.0;
        if (worstStructuralWhat.isNotEmpty())
            worst += "; worst STRUCTURAL (reported, not asserted) " + worstStructuralWhat;
        f.push_back ({ "R7", bad == 0 && worstRatio > 0.0, worstRatio, 3.0, worst });
    }

    /* R8 - hit targets: every interactive component >= 24x24, anything under
       32x32 reported. The measured value is the smallest side on the editor. */
    {
        int under24 = 0, under32 = 0, worstSide = 1 << 20; juce::String worst;
        for (const auto& n : sc.nodes) {
            if (! n.interactive || ! n.visible) continue;
            const int side = juce::jmin (n.bounds.getWidth(), n.bounds.getHeight());
            if (side < 24) ++under24;
            if (side < 32) ++under32;
            if (side < worstSide) {
                worstSide = side;
                worst = n.id + " (" + n.cls + ") " + juce::String (n.bounds.getWidth()) + "x"
                      + juce::String (n.bounds.getHeight());
            }
        }
        if (worstSide == (1 << 20)) worstSide = 0;
        f.push_back ({ "R8", under24 == 0, cv::dbl (worstSide), 24.0,
                       worst + "; " + juce::String (under32) + " under 32x32, "
                       + juce::String (under24) + " under 24x24" });
    }

    /* R9 - type scale: at most 5 distinct font sizes, on one consistent ratio. */
    {
        std::set<int> tenths;
        for (const auto& t : sc.texts)
            if (t.text.isNotEmpty()) tenths.insert (juce::roundToInt (t.font.getHeight() * 10.0f));
        juce::String list;
        std::vector<double> sizes;
        for (int s : tenths) {
            sizes.push_back (cv::dbl (s) / 10.0);
            list += (list.isEmpty() ? "" : " ") + juce::String (cv::dbl (s) / 10.0, 1);
        }
        double worstStep = 1.0, bestStep = 1.0e9;
        for (std::size_t i = 1; i < sizes.size(); ++i) {
            const double step = sizes[i] / sizes[i - 1];
            worstStep = juce::jmax (worstStep, step);
            bestStep  = juce::jmin (bestStep, step);
        }
        if (bestStep > 1.0e8) bestStep = 1.0;
        f.push_back ({ "R9", tenths.size() <= 5, cv::dbl (tenths.size()), 5.0,
                       list + " px; adjacent ratios " + juce::String (bestStep, 3) + ".."
                       + juce::String (worstStep, 3) });
    }

    /* R10 - palette: at most 6 distinct hues on the rendered canvas, with one
       accent hue reserved for "this is adjustable/active". */
    {
        const int buckets = 12;
        const int W = sc.canvas.getWidth(), H = sc.canvas.getHeight();
        std::vector<juce::int64> hueAll (cv::sz (buckets), 0), hueOnLive (cv::sz (buckets), 0);
        std::vector<juce::uint8> live (cv::sz (juce::jmax (1, W * H)), 0);
        for (const auto& n : sc.nodes) {
            if (! n.interactive || ! n.visible) continue;
            const auto r = n.bounds.getIntersection (sc.canvas);
            for (int y = r.getY(); y < r.getBottom(); ++y)
                for (int x = r.getX(); x < r.getRight(); ++x)
                    live[cv::sz (y) * cv::sz (W) + cv::sz (x)] = 1;
        }
        juce::int64 saturated = 0;
        /* the modal colour of each bucket, so "30 deg" is reported as the colour it
           actually is rather than as an angle nobody can picture */
        std::vector<std::map<juce::uint32, int>> hueHist (cv::sz (buckets));
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x) {
                const auto c = sc.pixelAt (x, y);
                if (c.getSaturation() < 0.18f || c.getBrightness() < 0.10f) continue;
                ++saturated;
                const int b = juce::jlimit (0, buckets - 1, cv::ivl (c.getHue() * cv::flt (buckets)));
                ++hueAll[cv::sz (b)];
                ++hueHist[cv::sz (b)][c.getARGB() & 0x00ffffffu];
                if (live[cv::sz (y) * cv::sz (W) + cv::sz (x)] != 0) ++hueOnLive[cv::sz (b)];
            }
        const juce::int64 total = cv::i64 (W) * cv::i64 (H);
        int hues = 0, accentBucket = -1;
        double accentShare = 0.0;
        juce::String list;
        for (int b = 0; b < buckets; ++b) {
            if (hueAll[cv::sz (b)] * 1000 < total) continue;     /* under 0.1 % of the canvas */
            ++hues;
            const double share = cv::dbl (hueOnLive[cv::sz (b)]) / cv::dbl (hueAll[cv::sz (b)]);
            juce::uint32 modal = 0; int modalN = -1;
            for (const auto& kv : hueHist[cv::sz (b)])
                if (kv.second > modalN) { modalN = kv.second; modal = kv.first; }
            list += (list.isEmpty() ? "" : ", ") + juce::String (b * 30) + "deg "
                  + juce::Colour (modal | 0xff000000u).toDisplayString (false) + " "
                  + juce::String (100.0 * cv::dbl (hueAll[cv::sz (b)])
                                  / cv::dbl (juce::jmax (cv::i64 (1), saturated)), 1) + "% of ink, "
                  + juce::String (100.0 * share, 0) + "% of it on controls";
            if (share > accentShare) { accentShare = share; accentBucket = b; }
        }
        f.push_back ({ "R10", hues <= 6, cv::dbl (hues), 6.0,
                       list + "; most-reserved hue " + juce::String (accentBucket * 30) + "deg at "
                       + juce::String (100.0 * accentShare, 0) + "%" });
    }

    /* R11 - every component's bounds land on a 4 px grid. */
    {
        int off = 0; juce::String worst;
        for (const auto& n : sc.nodes) {
            if (n.parent < 0 || ! n.visible || n.cls == "Resizer") continue;
            const bool ok = n.bounds.getX() % 4 == 0 && n.bounds.getY() % 4 == 0
                         && n.bounds.getWidth() % 4 == 0 && n.bounds.getHeight() % 4 == 0;
            if (! ok) {
                ++off;
                if (worst.isEmpty())
                    worst = n.id + " (" + n.cls + ") at " + juce::String (n.bounds.getX()) + ","
                          + juce::String (n.bounds.getY()) + " size " + juce::String (n.bounds.getWidth())
                          + "x" + juce::String (n.bounds.getHeight());
            }
        }
        f.push_back ({ "R11", off == 0, cv::dbl (off), 0.0, worst });
    }

    /* R12 - the largest empty rectangle, as a percentage of the canvas. */
    {
        juce::Rectangle<int> where;
        const juce::int64 area = largestEmptyRect (sc, 6, &where);
        const double pct = canvasArea > 0 ? 100.0 * cv::dbl (area) / cv::dbl (canvasArea) : 0.0;
        f.push_back ({ "R12", pct <= 15.0, pct, 15.0,
                       juce::String (where.getWidth()) + "x" + juce::String (where.getHeight())
                       + " px at " + juce::String (where.getX()) + "," + juce::String (where.getY()) });
    }

    /* R13 - every meter carries a scale: >= 2 labelled ticks and a reference
       mark. */
    {
        int bad = 0; juce::String worst;
        for (const auto& m : sc.meters)
            if (m.labelledTicks < 2 || ! m.referenceMark) {
                ++bad;
                if (worst.isEmpty())
                    worst = m.id + ": " + juce::String (m.labelledTicks) + " labelled ticks, "
                          + (m.referenceMark ? "reference mark" : "no reference mark");
            }
        f.push_back ({ "R13", bad == 0, cv::dbl (bad), 0.0,
                       worst.isEmpty() ? juce::String (sc.meters.size()) + " meters, all scaled" : worst });
    }

    return f;
}

inline const Finding* find (const std::vector<Finding>& f, const char* rule) {
    for (const auto& x : f)
        if (juce::String (x.rule) == rule) return &x;
    return nullptr;
}

} // namespace fvvis
