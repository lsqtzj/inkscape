/*
 * Inkscape::Text::Layout::Calculator - text layout engine meaty bits
 *
 * Authors:
 *   Richard Hughes <cyreve@users.sf.net>
 *
 * Copyright (C) 2005 Richard Hughes
 *
 * Released under GNU GPL, read the file 'COPYING' for more information
 */
#include "Layout-TNG.h"
#include "style.h"
#include "font-instance.h"
#include "svg/svg-types.h"
#include "sp-object.h"
#include "Layout-TNG-Scanline-Maker.h"
#include "FontFactory.h"
#include <pango/pango.h>
#include <map>

namespace Inkscape {
namespace Text {

//#define IFDEBUG(...)     __VA_ARGS__
#define IFDEBUG(...)

#define TRACE(format, ...) IFDEBUG(g_print(format, ## __VA_ARGS__),g_print("\n"))

// ******* enum conversion tables
static const Layout::EnumConversionItem enum_convert_spstyle_direction_to_pango_direction[] = {
	{SP_CSS_WRITING_MODE_LR_TB, PANGO_DIRECTION_LTR},
	{SP_CSS_WRITING_MODE_RL_TB, PANGO_DIRECTION_RTL},
	{SP_CSS_WRITING_MODE_TB_LR, PANGO_DIRECTION_LTR}};   // this is correct

static const Layout::EnumConversionItem enum_convert_spstyle_direction_to_my_direction[] = {
	{SP_CSS_WRITING_MODE_LR_TB, Layout::LEFT_TO_RIGHT},
	{SP_CSS_WRITING_MODE_RL_TB, Layout::RIGHT_TO_LEFT},
	{SP_CSS_WRITING_MODE_TB_LR, Layout::LEFT_TO_RIGHT}};   // this is correct

/** \brief private to Layout. Does the real work of text flowing.

This class does a standard greedy paragraph wrapping algorithm.

Very high-level overview:

<pre>
foreach(paragraph) {
  call pango_itemize() (_buildPangoItemizationForPara())
  break into spans, without dealing with wrapping (_buildSpansForPara())
  foreach(line in flow shape) {
    foreach(chunk in flow shape) {   (in _buildChunksInScanRun())
      // this inner loop in _measureUnbrokenSpan()
      if the line height changed discard the line and start again
      keep adding characters until we run out of space in the chunk, then back up to the last word boundary
      (do sensible things if there is no previous word break)
    }
    push all the glyphs, chars, spans, chunks and line to output (not completely trivial because we must draw rtl in character order) (in _outputLine())
  }
  push the paragraph (in calculate())
}
</pre>

...and all of that needs to work vertically too, and with all the little details that make life annoying
*/
class Layout::Calculator
{
    class SpanPosition;
    friend class SpanPosition;

    Layout &_flow;

    ScanlineMaker *_scanline_maker;

    unsigned _current_shape_index;     /// index into Layout::_input_wrap_shapes

    PangoContext *_pango_context;

    Direction _block_progression;

    /** for y= attributes in tspan elements et al, we do the adjustment by moving each
    glyph individually by this number. The spec means that this is maintained across
    paragraphs. */
    double _y_offset;

    /** to stop pango from hinting its output, the font factory creates all fonts very large.
    All numbers returned from pango have to be divided by this number \em and divided by
    PANGO_SCALE. See font_factory::font_factory(). */
    double _font_factory_size_multiplier;

    /** Temporary storage associated with each item in Layout::_input_stream. */
    struct InputItemInfo {
        bool in_sub_flow;
        Layout *sub_flow;    // this is only set for the first input item in a sub-flow

        InputItemInfo() : in_sub_flow(false), sub_flow(NULL) {}
        void free() {if (sub_flow) delete sub_flow; sub_flow = NULL;}
    };

    /** Temporary storage associated with each item returned by the call to
    pango_itemize(). */
    struct PangoItemInfo {
        PangoItem *item;
        font_instance *font;

        PangoItemInfo() : item(NULL), font(NULL) {}
        void free() {if (item) pango_item_free(item); item = NULL; if (font) font->Unref(); font = NULL;}
    };

    /** These spans have approximately the same definition as that used for
    Layout::Span (constant font, direction, etc), except that they are from
    before we have located the line breaks, so bear no relation to chunks.
    They are guaranteed to be in at most one PangoItem (spans with no text in
    them will not have an associated PangoItem), exactly one input source and
    will only have one change of x, y, dx, dy or rotate attribute, which will
    be at the beginning. An UnbrokenSpan can cross a chunk boundary, c.f.
    BrokenSpan. */
    struct UnbrokenSpan {
        PangoGlyphString *glyph_string;
        int pango_item_index;    /// index into _para.pango_items, or -1 if this is style only
        unsigned input_index;         /// index into Layout::_input_stream
        Glib::ustring::const_iterator input_stream_first_character;
        double font_size;
        LineHeight line_height;
        double line_height_multiplier;  /// calculated from the font-height css property
        unsigned text_bytes;
        unsigned char_index_in_para;    /// the index of the first character in this span in the paragraph, for looking up char_attributes
        SPSVGLength x, y, dx, dy, rotate;  // these are reoriented copies of the <tspan> attributes. We change span when we encounter one.

        UnbrokenSpan() : glyph_string(NULL) {}
        void free() {if (glyph_string) pango_glyph_string_free(glyph_string); glyph_string = NULL;}
    };

    /** a useful little iterator for moving char-by-char across spans. */
    struct UnbrokenSpanPosition {
        std::vector<UnbrokenSpan>::iterator iter_span;
        unsigned char_byte;
        unsigned char_index;

        void increment()   /// step forward by one character
        {
            gchar const *text_base = &*iter_span->input_stream_first_character.base();
            char_byte = g_utf8_next_char(text_base + char_byte) - text_base;
            char_index++;
            if (char_byte == iter_span->text_bytes) {
                iter_span++;
                char_index = char_byte = 0;
            }
        }

        inline bool operator== (UnbrokenSpanPosition const &other) const
            {return char_byte == other.char_byte && iter_span == other.iter_span;}
        inline bool operator!= (UnbrokenSpanPosition const &other) const
            {return char_byte != other.char_byte || iter_span != other.iter_span;}
    };

    /** The line breaking algorithm will convert each UnbrokenSpan into one
    or more of these. A BrokenSpan will never cross a chunk boundary, c.f.
    UnbrokenSpan. */
    struct BrokenSpan {
        UnbrokenSpanPosition start;
        UnbrokenSpanPosition end;    // the end of this will always be the same as the start of the next
        unsigned start_glyph_index;
        unsigned end_glyph_index;
        double width;
        unsigned whitespace_count;
        bool ends_with_whitespace;
        double each_whitespace_width;
        void setZero() {end = start; width = 0.0; whitespace_count = 0; end_glyph_index = start_glyph_index = 0; ends_with_whitespace = false; each_whitespace_width = 0.0;}
    };

    /** The definition of a chunk used here is the same as that used in Layout. */
    struct ChunkInfo {
        std::vector<BrokenSpan> broken_spans;
        double scanrun_width;
        double text_width;       /// that's the total width used by the text (excluding justification)
        double x;
        int whitespace_count;
    };

    /** Used to provide storage for anything that applies to the current
    paragraph only. Since we're only processing one paragraph at a time,
    there's only one instantiation of this struct, on the stack of
    calculate(). */
    struct ParagraphInfo {
        unsigned first_input_index;      /// index into Layout::_input_stream
        Direction direction;
        Alignment alignment;
        std::vector<InputItemInfo> input_items;
        std::vector<PangoItemInfo> pango_items;
        std::vector<PangoLogAttr> char_attributes;    /// for every character in the paragraph
        std::vector<UnbrokenSpan> unbroken_spans;

        template<typename T> static void free_vector(T *vec) {
            for (typename T::iterator it = vec->begin() ; it != vec->end() ; it++) it->free();
            vec->clear();
        }
        void free() {
            free_vector(&input_items);
            free_vector(&pango_items);
            free_vector(&unbroken_spans);
        }
    };

/* *********************************************************************************************************/
//                       Initialisation of ParagraphInfo structure

    
    /** for sections of text with a block-progression different to the rest
     * of the flow, the best thing to do is to detect them in advance and
     * create child TextFlow objects with just the rotated text. In the
     * parent we then effectively use ARBITRARY_GAP fields during the
     * flowing (because we don't allow wrapping when the block-progression
     * changes) and copy the actual text in during the output phase.
     * 
     * NB: this code not enabled yet.
     */
    void _initialiseInputItems(ParagraphInfo *para) const
    {
        Direction prev_block_progression = _block_progression;
        int run_start_input_index = para->first_input_index;

        para->free_vector(&para->input_items);
        for(int input_index = para->first_input_index ; input_index < (int)_flow._input_stream.size() ; input_index++) {
            InputItemInfo input_item;

            input_item.in_sub_flow = false;
            input_item.sub_flow = NULL;
            if (_flow._input_stream[input_index]->Type() == CONTROL_CODE) {
                Layout::InputStreamControlCode const *control_code = static_cast<Layout::InputStreamControlCode const *>(_flow._input_stream[input_index]);
                if (   control_code->code == SHAPE_BREAK
                    || control_code->code == PARAGRAPH_BREAK)
                    break;                                    // stop at the end of the paragraph
                // all other control codes we'll pick up later

            } else if (_flow._input_stream[input_index]->Type() == TEXT_SOURCE) {
                Layout::InputStreamTextSource *text_source = static_cast<Layout::InputStreamTextSource *>(_flow._input_stream[input_index]);
                Direction this_block_progression = text_source->styleGetBlockProgression();
                if (this_block_progression != prev_block_progression) {
                    if (prev_block_progression != _block_progression) {
                        // need to back up so that control codes belong outside the block-progression change
                        int run_end_input_index = input_index - 1;
                        while (run_end_input_index > run_start_input_index
                               && _flow._input_stream[run_end_input_index]->Type() != TEXT_SOURCE)
                            run_end_input_index--;
                        // now create the sub-flow
                        input_item.sub_flow = new Layout;
                        for (int sub_input_index = run_start_input_index ; sub_input_index <= run_end_input_index ; sub_input_index++) {
                            input_item.in_sub_flow = true;
                            if (_flow._input_stream[sub_input_index]->Type() == CONTROL_CODE) {
                                Layout::InputStreamControlCode const *control_code = static_cast<Layout::InputStreamControlCode const *>(_flow._input_stream[sub_input_index]);
                                input_item.sub_flow->appendControlCode(control_code->code, control_code->source_cookie, control_code->width, control_code->ascent, control_code->descent);
                            } else if (_flow._input_stream[sub_input_index]->Type() == TEXT_SOURCE) {
                                Layout::InputStreamTextSource *text_source = static_cast<Layout::InputStreamTextSource *>(_flow._input_stream[sub_input_index]);
                                input_item.sub_flow->appendText(*text_source->text, text_source->style, text_source->source_cookie, NULL, 0, text_source->text_begin, text_source->text_end);
                                Layout::InputStreamTextSource *sub_flow_text_source = static_cast<Layout::InputStreamTextSource *>(input_item.sub_flow->_input_stream.back());
                                sub_flow_text_source->x = text_source->x;    // this is easier than going via optionalattrs for the appendText() call
                                sub_flow_text_source->y = text_source->y;    // should these actually be allowed anyway? You'll almost never get the results you expect
                                sub_flow_text_source->dx = text_source->dx;  // (not that it's very clear what you should expect, anyway)
                                sub_flow_text_source->dy = text_source->dy;
                                sub_flow_text_source->rotate = text_source->rotate;
                            }
                        }
                        input_item.sub_flow->calculateFlow();
                    }
                    run_start_input_index = input_index;
                }
                prev_block_progression = this_block_progression;
            }
            para->input_items.push_back(input_item);
        }
    }

    /** take all the text from \a _para.first_input_index to the end of the
    paragraph and stitch it together so that pango_itemize() can be called on
    the whole thing.
    Input: para.first_input_index
    Output: para.direction, para.pango_items, para.char_attributes */
    void _buildPangoItemizationForPara(ParagraphInfo *para) const
    {
        Glib::ustring para_text;
        PangoAttrList *attributes_list;
        unsigned input_index;

        para->free_vector(&para->pango_items);
        para->char_attributes.clear();

        TRACE("itemizing para, first input %d", para->first_input_index);

        attributes_list = pango_attr_list_new();
        for(input_index = para->first_input_index ; input_index < _flow._input_stream.size() ; input_index++) {
            if (_flow._input_stream[input_index]->Type() == CONTROL_CODE) {
                Layout::InputStreamControlCode const *control_code = static_cast<Layout::InputStreamControlCode const *>(_flow._input_stream[input_index]);
                if (   control_code->code == SHAPE_BREAK
                    || control_code->code == PARAGRAPH_BREAK)
                    break;                                    // stop at the end of the paragraph
                // all other control codes we'll pick up later

            } else if (_flow._input_stream[input_index]->Type() == TEXT_SOURCE) {
                Layout::InputStreamTextSource *text_source = static_cast<Layout::InputStreamTextSource *>(_flow._input_stream[input_index]);

                // create the font_instance
                font_instance *font = text_source->styleGetFontInstance();
                if (font == NULL)
                    continue;  // bad news: we'll have to ignore all this text because we know of no font to render it

                PangoAttribute *attribute_font_description = pango_attr_font_desc_new(font->descr);
                attribute_font_description->start_index = para_text.bytes();
                para_text.append(&*text_source->text_begin.base(), text_source->text_length);     // build the combined text
                attribute_font_description->end_index = para_text.bytes();
                pango_attr_list_insert(attributes_list, attribute_font_description);
                  // ownership of attribute is assumed by the list
            }
        }

        TRACE("whole para: \"%s\"", para_text.data());
        TRACE("%d input sources used", input_index - para->first_input_index);

        // do the pango_itemize()
        GList *pango_items_glist = NULL;
        if (_flow._input_stream[para->first_input_index]->Type() == TEXT_SOURCE) {
            Layout::InputStreamTextSource const *text_source = static_cast<Layout::InputStreamTextSource *>(_flow._input_stream[para->first_input_index]);
            if (text_source->style->direction.set) {
                PangoDirection pango_direction = (PangoDirection)_enum_converter(text_source->style->direction.computed, enum_convert_spstyle_direction_to_pango_direction, sizeof(enum_convert_spstyle_direction_to_pango_direction)/sizeof(enum_convert_spstyle_direction_to_pango_direction[0]));
                pango_items_glist = pango_itemize_with_base_dir(_pango_context, pango_direction, para_text.data(), 0, para_text.bytes(), attributes_list, NULL);
                para->direction = (Layout::Direction)_enum_converter(text_source->style->direction.computed, enum_convert_spstyle_direction_to_my_direction, sizeof(enum_convert_spstyle_direction_to_my_direction)/sizeof(enum_convert_spstyle_direction_to_my_direction[0]));
            }
        }
        if (pango_items_glist == NULL) {  // no direction specified, guess it
            pango_items_glist = pango_itemize(_pango_context, para_text.data(), 0, para_text.bytes(), attributes_list, NULL);

            // I think according to the css spec this is wrong and we're never allowed to guess the directionality
            // of a paragraph. Need to talk to an rtl speaker.
            if (pango_items_glist == NULL || pango_items_glist->data == NULL) para->direction = LEFT_TO_RIGHT;
            else para->direction = (((PangoItem*)pango_items_glist->data)->analysis.level & 1) ? RIGHT_TO_LEFT : LEFT_TO_RIGHT;
        }
        pango_attr_list_unref(attributes_list);

        // convert the GList to our vector<> and make the font_instance for each PangoItem at the same time
        para->pango_items.reserve(g_list_length(pango_items_glist));
        TRACE("para itemizes to %d sections", g_list_length(pango_items_glist));
        for (GList *current_pango_item = pango_items_glist ; current_pango_item != NULL ; current_pango_item = current_pango_item->next) {
            PangoItemInfo new_item;
            new_item.item = (PangoItem*)current_pango_item->data;
            PangoFontDescription *font_description = pango_font_describe(new_item.item->analysis.font);
            new_item.font = (font_factory::Default())->Face(font_description);
            pango_font_description_free(font_description);   // Face() makes a copy
            para->pango_items.push_back(new_item);
        }
        g_list_free(pango_items_glist);

        // and get the character attributes on everything
        para->char_attributes.resize(para_text.length() + 1);
        pango_get_log_attrs(para_text.data(), para_text.bytes(), -1, NULL, &*para->char_attributes.begin(), para->char_attributes.size());

        TRACE("end para itemize, direction = %d", para->direction);
    }

    /** gets the ascent, descent and leading for a font and the alteration that
    has to be performed according to the value specified by the line-height css
    property. The result of multiplying \a line_height by \a line_height_multiplier
    is the inline box height as specified in css2 section 10.8. */
    static void _computeFontLineHeight(font_instance *font, double font_size, SPStyle const *style, LineHeight *line_height, double *line_height_multiplier)
    {
        if (font == NULL) {
            line_height->setZero();
            *line_height_multiplier = 1.0;
        }
        font->FontMetrics(line_height->ascent, line_height->descent, line_height->leading);
        *line_height *= font_size;

        *line_height_multiplier = 1.0;
        // yet another borked SPStyle member that we're going to have to fix ourselves
        for ( ; ; ) {
            if (style->line_height.set && !style->line_height.inherit) {
                switch (style->line_height.unit) {
                    case SP_CSS_UNIT_NONE:
                        *line_height_multiplier = style->line_height.computed * font_size / line_height->total();
                        break;
                    case SP_CSS_UNIT_EX:
                        *line_height_multiplier = style->line_height.value * 0.5 * font_size / line_height->total();
                                 // 0.5 is an approximation of the x-height. Fixme.
                        break;
                    case SP_CSS_UNIT_EM: 
                    case SP_CSS_UNIT_PERCENT:
                        *line_height_multiplier = style->line_height.value * font_size / line_height->total();
                        break;
                    default:  // absolute values
                        *line_height_multiplier = style->line_height.computed / line_height->total();
                        break;
                }
                break;
            }
            if (style->object->parent == NULL) break;
            style = style->object->parent->style;
        }
    }

    /** split the paragraph into spans. Also calls pango_shape()  on them.
    Input: para->first_input_index, para->pango_items
    Output: para->spans
    Returns: the index of the beginning of the following paragraph in _flow._input_stream */
    unsigned _buildSpansForPara(ParagraphInfo *para) const
    {
        unsigned pango_item_index = 0;
        unsigned char_index_in_para = 0;
        unsigned byte_index_in_para = 0;
        unsigned input_index;

        TRACE("build spans");
        para->free_vector(&para->unbroken_spans);

        for(input_index = para->first_input_index ; input_index < _flow._input_stream.size() ; input_index++) {
            if (_flow._input_stream[input_index]->Type() == CONTROL_CODE) {
                Layout::InputStreamControlCode const *control_code = static_cast<Layout::InputStreamControlCode const *>(_flow._input_stream[input_index]);
                if (   control_code->code == SHAPE_BREAK
                    || control_code->code == PARAGRAPH_BREAK)
                    break;                                    // stop at the end of the paragraph
                else if (control_code->code == ARBITRARY_GAP) {
                    UnbrokenSpan new_span;
                    new_span.pango_item_index = -1;
                    new_span.input_index = input_index;
                    new_span.line_height.ascent = control_code->ascent;
                    new_span.line_height.descent = control_code->descent;
                    new_span.line_height.leading = 0.0;
                    new_span.text_bytes = 0;
                    new_span.char_index_in_para = char_index_in_para;
                    para->unbroken_spans.push_back(new_span);
                    TRACE("add gap span %d", para->unbroken_spans.size() - 1);
                }
            } else if (_flow._input_stream[input_index]->Type() == TEXT_SOURCE && pango_item_index < para->pango_items.size()) {
                Layout::InputStreamTextSource const *text_source = static_cast<Layout::InputStreamTextSource const *>(_flow._input_stream[input_index]);
                unsigned char_index_in_source = 0;

                unsigned span_start_byte_in_source = 0;
                // we'll need to make several spans from each text source, based on the rules described about the UnbrokenSpan definition
                for ( ; ; ) {
                    UnbrokenSpan new_span;
                    unsigned pango_item_bytes;
                    unsigned text_source_bytes;
                    /* we need to change spans at every change of PangoItem, source stream change,
                    or change in one of the attributes altering position/rotation. */

                    pango_item_bytes = pango_item_index >= para->pango_items.size() ? 0 : para->pango_items[pango_item_index].item->offset + para->pango_items[pango_item_index].item->length - byte_index_in_para; 
                    text_source_bytes = text_source->text_end.base() - text_source->text_begin.base() - span_start_byte_in_source;
                    new_span.text_bytes = std::min(text_source_bytes, pango_item_bytes);
                    new_span.input_stream_first_character = Glib::ustring::const_iterator(text_source->text_begin.base() + span_start_byte_in_source);
                    new_span.char_index_in_para = char_index_in_para + char_index_in_source;
                    new_span.input_index = input_index;

                    // cut at <tspan> attribute changes as well
                    new_span.x.set = false;
                    new_span.y.set = false;
                    new_span.dx.set = false;
                    new_span.dy.set = false;
                    new_span.rotate.set = false;
                    if (_block_progression == TOP_TO_BOTTOM || _block_progression == BOTTOM_TO_TOP) {
                        if (text_source->x.size()  > char_index_in_source) new_span.x  = text_source->x[char_index_in_source];
                        if (text_source->y.size()  > char_index_in_source) new_span.y  = text_source->y[char_index_in_source];
                        if (text_source->dx.size() > char_index_in_source) new_span.dx = text_source->dx[char_index_in_source];
                        if (text_source->dy.size() > char_index_in_source) new_span.dy = text_source->dy[char_index_in_source];
                    } else {
                        if (text_source->x.size()  > char_index_in_source) new_span.y  = text_source->x[char_index_in_source];
                        if (text_source->y.size()  > char_index_in_source) new_span.x  = text_source->y[char_index_in_source];
                        if (text_source->dx.size() > char_index_in_source) new_span.dy = text_source->dx[char_index_in_source];
                        if (text_source->dy.size() > char_index_in_source) new_span.dx = text_source->dy[char_index_in_source];
                    }
                    if (text_source->rotate.size() > char_index_in_source) new_span.rotate = text_source->rotate[char_index_in_source];
                    Glib::ustring::const_iterator iter_text = new_span.input_stream_first_character;
                    iter_text++;
                    for (unsigned i = char_index_in_source + 1 ; ; i++, iter_text++) {
                        if (iter_text >= text_source->text_end) break;
                        if (iter_text.base() - new_span.input_stream_first_character.base() >= (int)new_span.text_bytes) break;
                        if (   i >= text_source->x.size() && i >= text_source->y.size()
                            && i >= text_source->dx.size() && i >= text_source->dy.size()
                            && i >= text_source->rotate.size()) break;
                        if (   (text_source->x.size()  > i && text_source->x[i].set)
                            || (text_source->y.size()  > i && text_source->y[i].set)
                            || (text_source->dx.size() > i && text_source->dx[i].set && text_source->dx[i].computed != 0.0)
                            || (text_source->dy.size() > i && text_source->dy[i].set && text_source->dy[i].computed != 0.0)
                            || (text_source->rotate.size() > i && text_source->rotate[i].set && text_source->rotate[i].computed != 0.0)) {
                            new_span.text_bytes = iter_text.base() - new_span.input_stream_first_character.base();
                            break;
                        }
                    }

                    // now we know the length, do some final calculations and add the UnbrokenSpan to the list
                    new_span.font_size = text_source->styleComputeFontSize();
                    if (new_span.text_bytes) {
                        int original_bidi_level = para->pango_items[pango_item_index].item->analysis.level;
                        para->pango_items[pango_item_index].item->analysis.level = 0;
                        // pango_shape() will reorder glyphs in rtl sections which messes us up because
                        // the svg spec requires us to draw glyphs in character order
                        new_span.glyph_string = pango_glyph_string_new();
                        pango_shape(text_source->text->data() + span_start_byte_in_source,
                                    new_span.text_bytes,
                                    &para->pango_items[pango_item_index].item->analysis,
                                    new_span.glyph_string);
                        para->pango_items[pango_item_index].item->analysis.level = original_bidi_level;
                        new_span.pango_item_index = pango_item_index;
                        _computeFontLineHeight(para->pango_items[pango_item_index].font, new_span.font_size, text_source->style, &new_span.line_height, &new_span.line_height_multiplier);
                        // TODO: metrics for vertical text
                        TRACE("add text span %d \"%s\"", para->unbroken_spans.size(), text_source->text->raw().substr(span_start_byte_in_source, new_span.text_bytes).c_str());
                        TRACE("  %d glyphs", new_span.glyph_string->num_glyphs);
                    } else {
                        // if there's no text we still need to initialise the styles
                        new_span.pango_item_index = -1;
                        font_instance *font = text_source->styleGetFontInstance();
                        if (font) {
                            _computeFontLineHeight(font, new_span.font_size, text_source->style, &new_span.line_height, &new_span.line_height_multiplier);
                            font->Unref();
                        } else {
                            new_span.line_height.setZero();
                            new_span.line_height_multiplier = 1.0;
                        }
                        TRACE("add style init span %d", para->unbroken_spans.size());
                    }
                    para->unbroken_spans.push_back(new_span);

                    // calculations for moving to the next UnbrokenSpan
                    byte_index_in_para += new_span.text_bytes;
                    char_index_in_source += g_utf8_strlen(&*new_span.input_stream_first_character.base(), new_span.text_bytes);

                    if (new_span.text_bytes >= pango_item_bytes) {   // end of pango item
                        pango_item_index++;
                        if (pango_item_index == para->pango_items.size()) break;  // end of paragraph
                    }
                    if (new_span.text_bytes == text_source_bytes)
                        break;    // end of source
                    // else <tspan> attribute changed
                    span_start_byte_in_source += new_span.text_bytes;
                }
                char_index_in_para += char_index_in_source;
            }
        }
        TRACE("end build spans");
        return input_index;
    }

/* *********************************************************************************************************/
//                             Per-line functions


    /** reinitialises the variables required on completion of one shape and
    moving on to the next. Returns false if there are no more shapes to wrap
    in to. */
    bool _goToNextWrapShape()
    {
        delete _scanline_maker;
        _scanline_maker = NULL;
        _current_shape_index++;
        if (_current_shape_index == _flow._input_wrap_shapes.size()) return false;
        _scanline_maker = new ShapeScanlineMaker(_flow._input_wrap_shapes[_current_shape_index].shape, _block_progression);
        TRACE("begin wrap shape %d", _current_shape_index);
        return true;
    }

    /** given \a para filled in and \a start_span_pos set, keeps trying to
    find somewhere it can fit the next line of text. The process of finding
    the text that fits will involve creating one or more entries in
    \a chunk_info describing the bounds of the fitted text and several
    bits of information that will prove useful when we come to output the
    line to #_flow. Returns with \a start_span_pos set to the end of the
    text that was fitted, \a chunk_info completely filled out and
    \a line_height set to the largest line box on the line. The return
    value is false only if we've run out of shapes to wrap inside (and
    hence couldn't create any chunks). */
    bool _findChunksForLine(ParagraphInfo const &para, UnbrokenSpanPosition *start_span_pos, std::vector<ChunkInfo> *chunk_info, LineHeight *line_height)
    {
        // init the initial line_height
        if (start_span_pos->iter_span == para.unbroken_spans.end()) {
            if (_flow._spans.empty()) {
                // empty first para: create a font for the sole purpose of measuring it
                InputStreamTextSource const *text_source = static_cast<InputStreamTextSource const *>(_flow._input_stream.front());
                font_instance *font = text_source->styleGetFontInstance();
                if (font) {
                    double font_size = text_source->styleComputeFontSize();
                    double multiplier;
                    _computeFontLineHeight(font, font_size, text_source->style, line_height, &multiplier);
                    font->Unref();
                    *line_height *= multiplier;
                    _scanline_maker->setNewYCoordinate(_scanline_maker->yCoordinate() - line_height->ascent);
                }
            }
            // else empty subsequent para: keep the old line height
        } else {
            if (_flow._input_wrap_shapes.empty()) {
                // if we're not wrapping set the line_height big and negative so we can use negative line height
                line_height->ascent = -1.0e10;
                line_height->descent = -1.0e10;
                line_height->leading = -1.0e10;
            }
            else
                line_height->setZero();
        }

        UnbrokenSpanPosition span_pos;
        for( ; ; ) {
            std::vector<ScanlineMaker::ScanRun> scan_runs;
            scan_runs = _scanline_maker->makeScanline(*line_height);
            while (scan_runs.empty()) {
                if (!_goToNextWrapShape()) return false;  // no more shapes to wrap in to
                scan_runs = _scanline_maker->makeScanline(*line_height);
            }

            TRACE("finding line fit y=%f, %d scan runs", scan_runs.front().y, scan_runs.size());
            chunk_info->clear();
            chunk_info->reserve(scan_runs.size());
            if (para.direction == RIGHT_TO_LEFT) std::reverse(scan_runs.begin(), scan_runs.end());
            unsigned scan_run_index;
            span_pos = *start_span_pos;
            for (scan_run_index = 0 ; scan_run_index < scan_runs.size() ; scan_run_index++) {
                if (!_buildChunksInScanRun(para, span_pos, scan_runs[scan_run_index], chunk_info, line_height))
                    break;
                if (!chunk_info->empty() && !chunk_info->back().broken_spans.empty())
                    span_pos = chunk_info->back().broken_spans.back().end;
            }
            if (scan_run_index == scan_runs.size()) break;  // ie when buildChunksInScanRun() succeeded
        }
        *start_span_pos = span_pos;
        return true;
    }

    static inline const PangoLogAttr& _charAttributes(ParagraphInfo const &para, UnbrokenSpanPosition const &span_pos)
    {
        return para.char_attributes[span_pos.iter_span->char_index_in_para + span_pos.char_index];
    }

    /** Given a scan run and a first character, append one or more chunks to
    the \a chunk_info vector that describe all the spans and other detail
    necessary to output the greatest amount of text that will fit on this scan
    line (greedy line breaking algorithm). Each chunk contains one or more
    BrokenSpan structures that link back to UnbrokenSpan structures that link
    to the text itself. Normally there will be either one or zero (if the
    scanrun is too short to fit any text) chunk added to \a chunk_info by
    each call to this method, but we will add more than one if an x or y
    attribute has been set on a tspan. \a line_height must be set on input,
    and if it needs to be made larger and the #_scanline_maker can't do
    an in-situ resize then it will be set to the required value and the
    method will return false. */
    bool _buildChunksInScanRun(ParagraphInfo const &para,
                               UnbrokenSpanPosition const &start_span_pos,
                               ScanlineMaker::ScanRun const &scan_run,
                               std::vector<ChunkInfo> *chunk_info,
                               LineHeight *line_height) const
    {
        BrokenSpan new_span, last_span_at_break;
        ChunkInfo new_chunk;

        new_chunk.text_width = 0.0;
        new_chunk.whitespace_count = 0;
        new_chunk.scanrun_width = scan_run.width();
        new_chunk.x = scan_run.x_start;

        // we haven't done anything yet so the last valid break position is the beginning
        last_span_at_break.start = start_span_pos;
        last_span_at_break.setZero();

        TRACE("trying chunk from %f to %g", scan_run.x_start, scan_run.x_end);
        new_span.end = start_span_pos;
        while (new_span.end.iter_span != para.unbroken_spans.end()) {    // this loops once for each UnbrokenSpan

            new_span.start = new_span.end;

            // force a chunk change at x or y attribute change
            if ((new_span.start.iter_span->x.set || new_span.start.iter_span->y.set) && new_span.start.char_byte == 0) {

                if (new_span.start.iter_span != start_span_pos.iter_span)
                    chunk_info->push_back(new_chunk);

                new_chunk.x += new_chunk.text_width;
                new_chunk.text_width = 0.0;
                new_chunk.whitespace_count = 0;
                if (new_span.start.iter_span->x.set) new_chunk.x = new_span.start.iter_span->x.computed;
                // y doesn't need to be done until output time
            }

            // see if this span is too tall to fit on the current line
            if (   new_span.start.iter_span->line_height.ascent * new_span.start.iter_span->line_height_multiplier  > line_height->ascent
                || new_span.start.iter_span->line_height.descent * new_span.start.iter_span->line_height_multiplier > line_height->descent
                || new_span.start.iter_span->line_height.leading * new_span.start.iter_span->line_height_multiplier > line_height->leading) {
                line_height->max(new_span.start.iter_span->line_height, new_span.start.iter_span->line_height_multiplier);
                if (!_scanline_maker->canExtendCurrentScanline(*line_height))
                    return false;
            }

            bool span_fitted = _measureUnbrokenSpan(para, &new_span, &last_span_at_break, new_chunk.scanrun_width - new_chunk.text_width);

            new_chunk.text_width += new_span.width;
            new_chunk.whitespace_count += new_span.whitespace_count;
            new_chunk.broken_spans.push_back(new_span);   // if !span_fitted we'll correct ourselves below

            if (!span_fitted) break;

            if (new_span.end.iter_span == para.unbroken_spans.end()) {
                last_span_at_break = new_span;
                break;
            }
        }

        TRACE("chunk complete, used %f width (%d whitespaces, %d brokenspans)", new_chunk.text_width, new_chunk.whitespace_count, new_chunk.broken_spans.size());
        chunk_info->push_back(new_chunk);

        if (!chunk_info->back().broken_spans.empty() && last_span_at_break.end != chunk_info->back().broken_spans.back().end) {
            // need to back out spans until we come to the one with the last break in it
            while (!chunk_info->empty() && last_span_at_break.start.iter_span != chunk_info->back().broken_spans.back().start.iter_span) {
                chunk_info->back().text_width -= chunk_info->back().broken_spans.back().width;
                chunk_info->back().whitespace_count -= chunk_info->back().broken_spans.back().whitespace_count;
                chunk_info->back().broken_spans.pop_back();
                if (chunk_info->back().broken_spans.empty())
                    chunk_info->pop_back();
            }
            if (!chunk_info->empty()) {
                chunk_info->back().text_width -= chunk_info->back().broken_spans.back().width;
                chunk_info->back().whitespace_count -= chunk_info->back().broken_spans.back().whitespace_count;
                if (last_span_at_break.start == last_span_at_break.end) {
                    chunk_info->back().broken_spans.pop_back();   // last break was at an existing boundary
                    if (chunk_info->back().broken_spans.empty())
                        chunk_info->pop_back();
                } else {
                    chunk_info->back().broken_spans.back() = last_span_at_break;
                    chunk_info->back().text_width += last_span_at_break.width;
                    chunk_info->back().whitespace_count += last_span_at_break.whitespace_count;
                }
                TRACE("correction: fitted span %d width = %f", last_span_at_break.start.iter_span - para.unbroken_spans.begin(), last_span_at_break.width);
            }
        }

        if (!chunk_info->empty() && !chunk_info->back().broken_spans.empty() && chunk_info->back().broken_spans.back().ends_with_whitespace) {
            // for justification we need to discard space occupied by the single whitespace at the end of the chunk
            chunk_info->back().broken_spans.back().ends_with_whitespace = false;
            chunk_info->back().broken_spans.back().width -= chunk_info->back().broken_spans.back().each_whitespace_width;
            chunk_info->back().broken_spans.back().whitespace_count--;
            chunk_info->back().text_width -= chunk_info->back().broken_spans.back().each_whitespace_width;
            chunk_info->back().whitespace_count--;
        }

        return true;
    }

    /** computes the width of a single UnbrokenSpan (pointed to by span->start.iter_span)
    and outputs its vital statistics into the other fields of \a span.
    Measuring will stop if maximum_width is reached and in that case the
    function will return false. In other cases where a line break must be
    done immediately the function will also return false. On return
    \a last_break_span will contain the vital statistics for the span only
    up to the last line breaking change. If there are no line breaking
    characters in the span then \a last_break_span will not be  altered. */
    bool _measureUnbrokenSpan(ParagraphInfo const &para, BrokenSpan *span, BrokenSpan *last_break_span, double maximum_width) const
    {
        span->setZero();

        if (span->start.iter_span->dx.set && span->start.char_byte == 0)
            span->width += span->start.iter_span->dx.computed;

        if (span->start.iter_span->pango_item_index == -1) {
            // if this is a style-only span there's no text in it
            // so we don't need to do very much at all
            span->end.iter_span++;
            return true;
        }

        if (_flow._input_stream[span->start.iter_span->input_index]->Type() == CONTROL_CODE) {
            InputStreamControlCode const *control_code = static_cast<InputStreamControlCode const *>(_flow._input_stream[span->start.iter_span->input_index]);
            if (control_code->code == SHAPE_BREAK || control_code->code == PARAGRAPH_BREAK) {
                *last_break_span = *span;
                return false;
            }
            if (control_code->code == ARBITRARY_GAP) {
                if (span->width + control_code->width > maximum_width)
                    return false;
                TRACE("fitted control code, width = %f", control_code->width);
                span->width += control_code->width;
                span->end.increment();
            }
            return true;

        }
        
        if (_flow._input_stream[span->start.iter_span->input_index]->Type() != TEXT_SOURCE)
            return true;  // never happens

        InputStreamTextSource const *text_source = static_cast<InputStreamTextSource const *>(_flow._input_stream[span->start.iter_span->input_index]);

        if (_directions_are_orthogonal(_block_progression, text_source->styleGetBlockProgression())) {
            // TODO: block-progression altered in the middle
            // Measure the precomputed flow from para.input_items
            return true;
        } 

        // a normal span going with a normal block-progression
        double font_size_multiplier = span->start.iter_span->font_size / (PANGO_SCALE * _font_factory_size_multiplier);
        double soft_hyphen_glyph_width = 0.0;
        bool soft_hyphen_in_word = false;
        bool is_soft_hyphen = false;
        IFDEBUG(int char_count = 0);

        // if we're not at the start of the span we need to pre-init glyph_index
        span->start_glyph_index = 0;
        while (span->start_glyph_index < (unsigned)span->start.iter_span->glyph_string->num_glyphs
               && span->start.iter_span->glyph_string->log_clusters[span->start_glyph_index] < (int)span->start.char_byte)
            span->start_glyph_index++;
        span->end_glyph_index = span->start_glyph_index;

        // go char-by-char summing the width, while keeping track of the previous break point
        do {
            PangoLogAttr const &char_attributes = _charAttributes(para, span->end);
            
            if (char_attributes.is_mandatory_break) {
                *last_break_span = *span;
                TRACE("span %d end of para; width = %f chars = %d", span->start.iter_span - para.unbroken_spans.begin(), span->width, char_count);
                return false;
            }

            if (char_attributes.is_line_break || char_attributes.is_white || is_soft_hyphen) {
                // a suitable position to break at, record where we are
                *last_break_span = *span;
                if (soft_hyphen_in_word) {
                    // if there was a previous soft hyphen we're not going to need it any more so we can remove it
                    span->width -= soft_hyphen_glyph_width;
                    if (!is_soft_hyphen)
                        soft_hyphen_in_word = false;
                }
            }
            // todo: break between chars if necessary (ie no word breaks present) when doing rectangular flowing

            // sum the glyph widths, letter spacing and word spacing to get the character width
            double char_width = 0.0;
            while (span->end_glyph_index < (unsigned)span->end.iter_span->glyph_string->num_glyphs
                   && span->end.iter_span->glyph_string->log_clusters[span->end_glyph_index] <= (int)span->end.char_byte) {
                char_width += span->end.iter_span->glyph_string->glyphs[span->end_glyph_index].geometry.width;
                span->end_glyph_index++;
            }
            char_width *= font_size_multiplier;
            if (char_attributes.is_cursor_position)
                char_width += text_source->style->letter_spacing.computed;
            if (char_attributes.is_white)
                char_width += text_source->style->word_spacing.computed;
            span->width += char_width;
            IFDEBUG(char_count++);

            if (char_attributes.is_white) {
                span->whitespace_count++;
                span->each_whitespace_width = char_width;
            }
            span->ends_with_whitespace = char_attributes.is_white;

            is_soft_hyphen = (UNICODE_SOFT_HYPHEN == *Glib::ustring::const_iterator(span->end.iter_span->input_stream_first_character.base() + span->end.char_byte));
            if (is_soft_hyphen)
                soft_hyphen_glyph_width = char_width;

            span->end.increment();

            if (span->width > maximum_width && !char_attributes.is_white) {       // whitespaces don't matter, we can put as many as we want at eol
                TRACE("span %d exceeded scanrun; width = %f chars = %d", span->start.iter_span - para.unbroken_spans.begin(), span->width, char_count);
                return false;
            }

        } while (span->end.char_byte != 0);  // while we haven't wrapped to the next span
        TRACE("fitted span %d width = %f chars = %d", span->start.iter_span - para.unbroken_spans.begin(), span->width, char_count);
        return true;
    }

/* *********************************************************************************************************/
//                             Per-line functions (output)

    /** Uses the paragraph alignment and the chunk information to work out
    where the actual left of the final chunk must be. Also sets
    \a add_to_each_whitespace to be the amount of x to add at each
    whitespace character to make full justification work. */
    double _getChunkLeftWithAlignment(ParagraphInfo const &para, std::vector<ChunkInfo>::const_iterator it_chunk, double *add_to_each_whitespace) const
    {
        *add_to_each_whitespace = 0.0;
        if (_flow._input_wrap_shapes.empty()) {
            switch (para.alignment) {
                case FULL:
                case LEFT:
                default:
                    return it_chunk->x;
                case RIGHT:
                    return it_chunk->x - it_chunk->text_width;
                case CENTER:
                    return it_chunk->x - it_chunk->text_width / 2;
            }
        }

        switch (para.alignment) {
            case FULL:
                if (it_chunk->broken_spans.back().end.iter_span != para.unbroken_spans.end()) {   // don't justify the last chunk in the para
                    if (it_chunk->whitespace_count)
                        *add_to_each_whitespace = (it_chunk->scanrun_width - it_chunk->text_width) / it_chunk->whitespace_count;
                    //else 
                        //add_to_each_charspace = something
                }
                return it_chunk->x;
            case LEFT:
            default:
                return it_chunk->x;
            case RIGHT:
                return it_chunk->x + it_chunk->scanrun_width - it_chunk->text_width;
            case CENTER:
                return it_chunk->x + (it_chunk->scanrun_width - it_chunk->text_width) / 2;
        }
    }

    /** Once we've got here we have finished making changes to the line and
    are ready to output the final result to #_flow. This method takes its
    input parameters and does that.
    */
    void _outputLine(ParagraphInfo const &para, LineHeight const &line_height, std::vector<ChunkInfo> const &chunk_info)
    {
        if (chunk_info.empty()) {
            TRACE("line too short to fit anything on it, go to next");
            return;
        }

        // we've finished fiddling about with ascents and descents: create the output
        TRACE("found line fit; creating output");
        Layout::Line new_line;
        new_line.in_paragraph = _flow._paragraphs.size() - 1;
        new_line.baseline_y = _scanline_maker->yCoordinate() + line_height.ascent;
        new_line.in_shape = _current_shape_index;
        _flow._lines.push_back(new_line);

        for (std::vector<ChunkInfo>::const_iterator it_chunk = chunk_info.begin() ; it_chunk != chunk_info.end() ; it_chunk++) {

            double add_to_each_whitespace;
            // add the chunk to the list
            Layout::Chunk new_chunk;
            new_chunk.in_line = _flow._lines.size() - 1;
            new_chunk.left_x = _getChunkLeftWithAlignment(para, it_chunk, &add_to_each_whitespace);
            // we may also have y move orders to deal with here (dx, dy and rotate are done per span)
            if (!it_chunk->broken_spans.empty()    // this one only happens for empty paragraphs
                && it_chunk->broken_spans.front().start.char_byte == 0
                && it_chunk->broken_spans.front().start.iter_span->y.set) {
                // if this is the start of a line, we should change the baseline rather than each glyph individually
                if (_flow._characters.empty() || _flow._characters.back().chunk(&_flow).in_line != _flow._lines.size() - 1) {
                    new_line.baseline_y = it_chunk->broken_spans.front().start.iter_span->y.computed;
                    _flow._lines.back().baseline_y = new_line.baseline_y;
                    _y_offset = 0.0;
                    _scanline_maker->setNewYCoordinate(new_line.baseline_y - line_height.ascent);
                } else
                    _y_offset = it_chunk->broken_spans.front().start.iter_span->y.computed - new_line.baseline_y;
            }
            _flow._chunks.push_back(new_chunk);

            double x;
            double direction_sign;
            if (para.direction == LEFT_TO_RIGHT) {
                direction_sign = +1.0;
                x = 0.0;
            } else {
                direction_sign = -1.0;
                if (para.alignment == FULL && !_flow._input_wrap_shapes.empty())
                    x = it_chunk->scanrun_width;
                else
                    x = it_chunk->text_width;
            }

            for (std::vector<BrokenSpan>::const_iterator it_span = it_chunk->broken_spans.begin() ; it_span != it_chunk->broken_spans.end() ; it_span++) {
                // begin adding spans to the list
                Direction previous_direction = para.direction;
                double counter_directional_width_remaining = 0.0;
                float glyph_rotate = 0.0;
                UnbrokenSpan const &unbroken_span = *it_span->start.iter_span;

                if (it_span->start.char_byte == 0) {
                    // start of an unbroken span, we might have dx, dy or rotate still to process (x and y are done per chunk)
                    if (unbroken_span.dx.set) x += unbroken_span.dx.computed;
                    if (unbroken_span.dy.set) _y_offset += unbroken_span.dy.computed;
                    if (unbroken_span.rotate.set) glyph_rotate = unbroken_span.rotate.computed;
                }

                if (_flow._input_stream[unbroken_span.input_index]->Type() == TEXT_SOURCE
                    && unbroken_span.pango_item_index == -1) {
                    // style only, nothing to output
                    continue;
                }

                Layout::Span new_span;
                double x_in_span = 0.0;

                new_span.in_chunk = _flow._chunks.size() - 1;
                new_span.line_height = unbroken_span.line_height;
                new_span.in_input_stream_item = unbroken_span.input_index;
                new_span.x_start = x;
                new_span.baseline_shift = _y_offset;
                new_span.block_progression = _block_progression;
                if (_flow._input_stream[unbroken_span.input_index]->Type() == TEXT_SOURCE) {
                    new_span.font = para.pango_items[unbroken_span.pango_item_index].font;
                    new_span.font->Ref();
                    new_span.font_size = unbroken_span.font_size;
                    new_span.direction = para.pango_items[unbroken_span.pango_item_index].item->analysis.level & 1 ? RIGHT_TO_LEFT : LEFT_TO_RIGHT;
                    new_span.input_stream_first_character = Glib::ustring::const_iterator(unbroken_span.input_stream_first_character.base() + it_span->start.char_byte);
                } else {  // a control code
                    new_span.font = NULL;
                    new_span.font_size = new_span.line_height.ascent + new_span.line_height.descent;
                    new_span.direction = para.direction;
                }

                if (new_span.direction == para.direction)
                    counter_directional_width_remaining = 0.0;
                else if (new_span.direction != previous_direction) {
                    // measure width of spans we need to switch round
                    counter_directional_width_remaining = 0.0;
                    std::vector<BrokenSpan>::const_iterator it_following_span;
                    for (it_following_span = it_span ; it_following_span != it_chunk->broken_spans.end() ; it_following_span++) {
                        Layout::Direction following_span_progression = static_cast<InputStreamTextSource const *>(_flow._input_stream[it_following_span->start.iter_span->input_index])->styleGetBlockProgression();
                        if (!Layout::_directions_are_orthogonal(following_span_progression, _block_progression)) {
                            if (it_following_span->start.iter_span->pango_item_index == -1) {   // when the span came from a control code
                                if (new_span.direction != para.direction) break;
                            } else
                                if (new_span.direction != (para.pango_items[it_following_span->start.iter_span->pango_item_index].item->analysis.level & 1 ? RIGHT_TO_LEFT : LEFT_TO_RIGHT)) break;
                        }
                        counter_directional_width_remaining += direction_sign * (it_following_span->width + it_following_span->whitespace_count * add_to_each_whitespace);
                    }
                    x += counter_directional_width_remaining;
                    counter_directional_width_remaining = 0.0;    // we want to go increasingly negative
                }

                if (_flow._input_stream[unbroken_span.input_index]->Type() == TEXT_SOURCE) {
                    // the span is set up, push the glyphs and chars
                    InputStreamTextSource const *text_source = static_cast<InputStreamTextSource const *>(_flow._input_stream[unbroken_span.input_index]);
                    Glib::ustring::const_iterator iter_source_text = Glib::ustring::const_iterator(unbroken_span.input_stream_first_character.base() + it_span->start.char_byte) ;
                    unsigned char_index_in_unbroken_span = it_span->start.char_index;
                    double font_size_multiplier = new_span.font_size / (PANGO_SCALE * _font_factory_size_multiplier);

                    for (unsigned glyph_index = it_span->start_glyph_index ; glyph_index < it_span->end_glyph_index ; glyph_index++) {
                        unsigned char_byte = iter_source_text.base() - unbroken_span.input_stream_first_character.base();

                        if (unbroken_span.glyph_string->log_clusters[glyph_index] < (int)unbroken_span.text_bytes
                            && *iter_source_text == UNICODE_SOFT_HYPHEN
                            && it_span + 1 != it_chunk->broken_spans.end()
                            && glyph_index + 1 != it_span->end_glyph_index) {
                            // if we're looking at a soft hyphen and it's not the last glyph in the
                            // chunk we don't draw the glyph but we still need to add to _characters
                            Layout::Character new_character;
                            new_character.in_span = _flow._spans.size();     // the span hasn't been added yet, so no -1
                            new_character.char_attributes = para.char_attributes[unbroken_span.char_index_in_para + char_index_in_unbroken_span];
                            new_character.in_glyph = -1;
                            _flow._characters.push_back(new_character);
                            iter_source_text++;
                            char_index_in_unbroken_span++;
                            while (glyph_index < (unsigned)unbroken_span.glyph_string->num_glyphs
                                   && unbroken_span.glyph_string->log_clusters[glyph_index] == (int)char_byte)
                                glyph_index++;
                            glyph_rotate = 0.0;
                            glyph_index--;
                            continue;
                        }

                        // create the Layout::Glyph
                        Layout::Glyph new_glyph;
                        new_glyph.glyph = unbroken_span.glyph_string->glyphs[glyph_index].glyph;
                        new_glyph.in_character = _flow._characters.size();
                        new_glyph.rotation = glyph_rotate;
                        new_glyph.x = x + counter_directional_width_remaining + unbroken_span.glyph_string->glyphs[glyph_index].geometry.x_offset * font_size_multiplier;
                        new_glyph.y = _y_offset + unbroken_span.glyph_string->glyphs[glyph_index].geometry.y_offset * font_size_multiplier;

                        /* put this back in when we do glyph-rotation-horizontal/vertical
                        if (new_span.block_progression == LEFT_TO_RIGHT || new_span.block_progression == RIGHT_TO_LEFT) {
                            new_glyph.x += new_span.line_height.ascent;
                            new_glyph.y -= unbroken_span.glyph_string->glyphs[glyph_index].geometry.width * font_size_multiplier * 0.5;
                            new_glyph.width = new_span.line_height.ascent + new_span.line_height.descent;
                        } else */

                        new_glyph.width = unbroken_span.glyph_string->glyphs[glyph_index].geometry.width * font_size_multiplier;
                        if (new_span.direction == RIGHT_TO_LEFT) new_glyph.x -= new_glyph.width;
                        _flow._glyphs.push_back(new_glyph);

                        // create the Layout::Character(s)
                        double advance_width = new_glyph.width;
                        unsigned end_byte;
                        if (glyph_index == (unsigned)unbroken_span.glyph_string->num_glyphs - 1)
                            end_byte = it_span->start.iter_span->text_bytes;
                        else end_byte = unbroken_span.glyph_string->log_clusters[glyph_index + 1];
                        while (char_byte < end_byte) {
                            Layout::Character new_character;
                            new_character.in_span = _flow._spans.size();
                            new_character.x = x_in_span;
                            new_character.char_attributes = para.char_attributes[unbroken_span.char_index_in_para + char_index_in_unbroken_span];
                            new_character.in_glyph = _flow._glyphs.size() - 1;
                            _flow._characters.push_back(new_character);
                            if (new_character.char_attributes.is_white)
                                advance_width += text_source->style->word_spacing.computed + add_to_each_whitespace;    // justification
                            if (new_character.char_attributes.is_cursor_position)
                                advance_width += text_source->style->letter_spacing.computed;
                            iter_source_text++;
                            char_index_in_unbroken_span++;
                            char_byte = iter_source_text.base() - unbroken_span.input_stream_first_character.base();
                            glyph_rotate = 0.0;    // only the first glyph is rotated because of how we split the spans
                        }

                        advance_width *= direction_sign;
                        if (new_span.direction != para.direction) {
                            counter_directional_width_remaining -= advance_width;
                            x_in_span -= advance_width;
                        } else {
                            x += advance_width;
                            x_in_span += advance_width;
                        }
                    }
                } else if (_flow._input_stream[unbroken_span.input_index]->Type() == CONTROL_CODE) {
                    x += static_cast<InputStreamControlCode const *>(_flow._input_stream[unbroken_span.input_index])->width;
                }

                if (new_span.direction != para.direction) {
                    new_span.x_end = new_span.x_start;
                    new_span.x_start = new_span.x_end - it_span->width - add_to_each_whitespace * it_span->whitespace_count;
                } else
                    new_span.x_end = new_span.x_start + x_in_span;
                _flow._spans.push_back(new_span);
                previous_direction = new_span.direction;
            }
            // end adding spans to the list, on to the next chunk...
        }
        TRACE("output done");
    }

/* *********************************************************************************************************/
//                             Setup and top-level functions

    /** initialises the ScanlineMaker for the first shape in the flow, or
    the infinite version if we're not doing wrapping. */
    void _createFirstScanlineMaker()
    {
        _current_shape_index = 0;
        if (_flow._input_wrap_shapes.empty()) {
            // create the special no-wrapping infinite scanline maker
            double initial_x = 0, initial_y = 0;
            InputStreamTextSource const *text_source = static_cast<InputStreamTextSource const *>(_flow._input_stream.front());
            if (!text_source->x.empty())
                initial_x = text_source->x.front().computed;
            if (!text_source->y.empty())
                initial_y = text_source->y.front().computed;
            _scanline_maker = new InfiniteScanlineMaker(initial_x, initial_y, _block_progression);
            TRACE("  wrapping disabled");
        }
        else {
            _scanline_maker = new ShapeScanlineMaker(_flow._input_wrap_shapes[_current_shape_index].shape, _block_progression);
            TRACE("  begin wrap shape 0");
        }
    }

public:
    Calculator(Layout *text_flow)
        : _flow(*text_flow) {}

    /** The management function to start the whole thing off. */
    bool calculate()
    {
        if (_flow._input_stream.empty())
            return false;
        g_assert(_flow._input_stream.front()->Type() == TEXT_SOURCE);
        if (_flow._input_stream.front()->Type() != TEXT_SOURCE)
            return false;

        TRACE("begin calculateFlow()");

        _flow._clearOutputObjects();
        
        _pango_context = (font_factory::Default())->fontContext;
        _font_factory_size_multiplier = (font_factory::Default())->fontSize;

        _block_progression = _flow._blockProgression();
        _y_offset = 0.0;
        _createFirstScanlineMaker();

        ParagraphInfo para;
        LineHeight line_height;     // needs to be maintained across paragraphs to be able to deal with blank paras (this is wrong)
        for(para.first_input_index = 0 ; para.first_input_index < _flow._input_stream.size() ; ) {
            // jump to the next wrap shape if this is a SHAPE_BREAK control code
            if (_flow._input_stream[para.first_input_index]->Type() == CONTROL_CODE) {
                InputStreamControlCode const *control_code = static_cast<InputStreamControlCode const *>(_flow._input_stream[para.first_input_index]);
                if (control_code->code == SHAPE_BREAK) {
                    TRACE("shape break control code");
                    if (!_goToNextWrapShape()) break;
                    continue;
                }
            }
            if (_scanline_maker == NULL)
                break;       // we're trying to flow past the last wrap shape

            _buildPangoItemizationForPara(&para);
            unsigned para_end_input_index = _buildSpansForPara(&para);

            if (_flow._input_stream[para.first_input_index]->Type() == TEXT_SOURCE)
                para.alignment = static_cast<InputStreamTextSource*>(_flow._input_stream[para.first_input_index])->styleGetAlignment(para.direction);
            else
                para.alignment = para.direction == LEFT_TO_RIGHT ? LEFT : RIGHT;

            TRACE("para prepared, adding as #%d", _flow._paragraphs.size());
            Layout::Paragraph new_paragraph;
            new_paragraph.base_direction = para.direction;
            _flow._paragraphs.push_back(new_paragraph);

            // start scanning lines
            UnbrokenSpanPosition span_pos;
            span_pos.iter_span = para.unbroken_spans.begin();
            span_pos.char_byte = 0;
            span_pos.char_index = 0;

            do {   // for each line in the paragraph
                TRACE("begin line");
                std::vector<ChunkInfo> line_chunk_info;
                if (!_findChunksForLine(para, &span_pos, &line_chunk_info, &line_height))
                    break;   // out of shapes to wrap in to

                _outputLine(para, line_height, line_chunk_info);
                _scanline_maker->completeLine();
            } while (span_pos.iter_span != para.unbroken_spans.end());

            TRACE("para %d end\n", _flow._paragraphs.size() - 1);
            if (_scanline_maker != NULL) {
                bool is_empty_para = _flow._characters.empty() || _flow._characters.back().line(&_flow).in_paragraph != _flow._paragraphs.size() - 1;
                if ((is_empty_para && para_end_input_index + 1 >= _flow._input_stream.size())
                    || para_end_input_index + 1 < _flow._input_stream.size()) {
                    // we need a span just for the para if it's either an empty last para or a break in the middle
                    Layout::Span new_span;
                    if (_flow._spans.empty()) {
                        new_span.font = NULL;
                        new_span.font_size = line_height.ascent + line_height.descent;
                        new_span.line_height = line_height;
                        new_span.x_end = 0.0;
                    } else {
                        new_span = _flow._spans.back();
                        if (_flow._chunks[new_span.in_chunk].in_line != _flow._lines.size() - 1)
                            new_span.x_end = 0.0;
                    }
                    new_span.in_chunk = _flow._chunks.size() - 1;
                    if (new_span.font)
                        new_span.font->Ref();
                    new_span.x_start = new_span.x_end;
                    new_span.direction = para.direction;
                    new_span.block_progression = _block_progression;
                    if (para_end_input_index == _flow._input_stream.size())
                        new_span.in_input_stream_item = _flow._input_stream.size() - 1;
                    else
                        new_span.in_input_stream_item = para_end_input_index;
                    _flow._spans.push_back(new_span);
                }
                if (para_end_input_index + 1 < _flow._input_stream.size()) {
                    // we've got to add an invisible character between paragraphs so that we can position iterators
                    // (and hence cursors) both before and after the paragraph break
                    Layout::Character new_character;
                    new_character.in_span = _flow._spans.size() - 1;
                    new_character.char_attributes.is_line_break = 1;
                    new_character.char_attributes.is_mandatory_break = 1;
                    new_character.char_attributes.is_char_break = 1;
                    new_character.char_attributes.is_white = 1;
                    new_character.char_attributes.is_cursor_position = 1;
                    new_character.char_attributes.is_word_start = 0;
                    new_character.char_attributes.is_word_end = 0;
                    new_character.char_attributes.is_sentence_start = 0;
                    new_character.char_attributes.is_sentence_end = 0;
                    new_character.char_attributes.is_sentence_boundary = 1;
                    new_character.char_attributes.backspace_deletes_character = 1;
                    new_character.x = _flow._spans.back().x_end - _flow._spans.back().x_start;
                    new_character.in_glyph = -1;
                    _flow._characters.push_back(new_character);
                }
            }
            para.free();
            para.first_input_index = para_end_input_index + 1;
        }

        para.free();
        if (_scanline_maker)
            delete _scanline_maker;
        
        return true;
    }
};

void Layout::_calculateCursorShapeForEmpty()
{
    _empty_cursor_shape.position = NR::Point(0, 0);
    _empty_cursor_shape.height = 0.0;
    _empty_cursor_shape.rotation = 0.0;
    if (_input_stream.empty() || _input_stream.front()->Type() != TEXT_SOURCE)
        return;

    InputStreamTextSource const *text_source = static_cast<InputStreamTextSource const *>(_input_stream.front());

    font_instance *font = text_source->styleGetFontInstance();
    double font_size = text_source->styleComputeFontSize();
    double caret_slope_run = 0.0, caret_slope_rise = 1.0;
    LineHeight line_height;
    if (font) {
        const_cast<font_instance*>(font)->FontSlope(caret_slope_run, caret_slope_rise);
        font->FontMetrics(line_height.ascent, line_height.descent, line_height.leading);
        line_height *= font_size;
        font->Unref();
    } else {
        line_height.ascent = font_size * 0.85;      // random guesses
        line_height.descent = font_size * 0.15;
        line_height.leading = 0.0;
    }
    double caret_slope = atan2(caret_slope_run, caret_slope_rise);
    _empty_cursor_shape.height = font_size / cos(caret_slope);
    _empty_cursor_shape.rotation = caret_slope;

    if (_input_wrap_shapes.empty()) {
        _empty_cursor_shape.position = NR::Point(text_source->x.empty() || !text_source->x.front().set ? 0.0 : text_source->x.front().computed,
                                                 text_source->y.empty() || !text_source->y.front().set ? 0.0 : text_source->y.front().computed);
    } else {
        Direction block_progression = text_source->styleGetBlockProgression();
        ShapeScanlineMaker scanline_maker(_input_wrap_shapes.front().shape, block_progression);
        std::vector<ScanlineMaker::ScanRun> scan_runs = scanline_maker.makeScanline(line_height);
        if (!scan_runs.empty()) {
            if (block_progression == LEFT_TO_RIGHT || block_progression == RIGHT_TO_LEFT)
                _empty_cursor_shape.position = NR::Point(scan_runs.front().y + font_size, scan_runs.front().x_start);
            else
                _empty_cursor_shape.position = NR::Point(scan_runs.front().x_start, scan_runs.front().y + font_size);
        }
    }
}

bool Layout::calculateFlow()
{
    bool result = Calculator(this).calculate();
    if (_characters.empty())
        _calculateCursorShapeForEmpty();
    return result;
}

}//namespace Text
}//namespace Inkscape
