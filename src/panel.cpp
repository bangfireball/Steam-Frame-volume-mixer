#include "panel.h"
#include <cairo/cairo.h>
#include <fstream>
#include <stdexcept>

namespace mixer {
namespace {
struct Canvas {
    cairo_surface_t* surface;
    cairo_t* c;
    int width, height;
    Canvas(int w, int h) : surface(cairo_image_surface_create(CAIRO_FORMAT_ARGB32,w,h)), c(cairo_create(surface)), width(w), height(h) {
        if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS || cairo_status(c) != CAIRO_STATUS_SUCCESS) throw std::runtime_error("Cairo canvas allocation failed");
        cairo_select_font_face(c, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    }
    ~Canvas() { cairo_destroy(c); cairo_surface_destroy(surface); }
    void color(double r, double g, double b) { cairo_set_source_rgb(c,r,g,b); }
    void box(double x,double y,double w,double h) { cairo_rectangle(c,x,y,w,h); cairo_fill(c); }
    void text(double x,double y,const std::string& text,int size=23, double maxWidth=920) {
        cairo_save(c); cairo_rectangle(c,x,y-size,maxWidth,size+9); cairo_clip(c);
        cairo_set_font_size(c,size); cairo_move_to(c,x,y); cairo_show_text(c,safeLabel(text).c_str()); cairo_restore(c);
    }
    std::vector<std::uint8_t> pixels() {
        cairo_surface_flush(surface);
        auto* data = cairo_image_surface_get_data(surface); auto stride = cairo_image_surface_get_stride(surface);
        std::vector<std::uint8_t> result(static_cast<std::size_t>(width)*height*4);
        for (int y=0; y<height; ++y) for (int x=0; x<width; ++x) {
            auto* p = reinterpret_cast<const std::uint32_t*>(data+y*stride)+x;
            auto i = static_cast<std::size_t>(y*width+x)*4;
            result[i] = (*p>>16)&255; result[i+1] = (*p>>8)&255; result[i+2] = *p&255; result[i+3] = (*p>>24)&255;
        }
        return result;
    }
};
}
std::vector<std::uint8_t> renderPanel(const Interaction& ui, bool confirmQuit, bool mock) {
    Canvas cv(Interaction::width, Interaction::height);
    cv.color(.055,.075,.11); cv.box(0,0,cv.width,cv.height);
    cv.color(.95,.97,1); cv.text(32,49,"Frame Volume Mixer",34);
    cv.color(.5,.8,.85); cv.text(33,86,mock ? "DEMO — no real audio changes" : ui.snapshot().status,20);
    auto drawControls = [&](Key key, bool muted, bool writable, bool pending, int value, int top) {
        cv.color(muted ? .45 : .19, muted ? .24 : .28, muted ? .24 : .36); cv.box(465,top+12,85,48);
        cv.color(1,1,1); cv.text(475,top+43,muted ? "Muted" : "Mute",19,76);
        cv.color(.25,.31,.4); cv.box(Interaction::sliderX,top+34,Interaction::sliderEnd-Interaction::sliderX,8);
        if (ui.dragging() == key) value = ui.dragPercent();
        const double fraction = std::clamp(value/100.0,0.0,1.0);
        cv.color(writable ? .25 : .4, writable ? .78 : .4, writable ? .87 : .4);
        cv.box(Interaction::sliderX,top+34,(Interaction::sliderEnd-Interaction::sliderX)*fraction,8);
        cairo_arc(cv.c,Interaction::sliderX+(Interaction::sliderEnd-Interaction::sliderX)*fraction,top+38,12,0,6.283185); cairo_fill(cv.c);
        cv.color(.94,.96,1); cv.text(892,top+43,writable ? std::to_string(value)+"%" : "N/A",21,75);
        if (pending) { cv.color(.9,.75,.35); cv.text(585,top+63,"Applying...",13); }
    };
    cv.color(.10,.20,.26); cv.box(24,Interaction::masterY,952,74);
    cv.color(.94,.96,1); cv.text(48,Interaction::masterY+29,"Master — Default output",24,395);
    cv.color(.57,.70,.77); cv.text(48,Interaction::masterY+56,ui.snapshot().master ? ui.snapshot().master->description : "Output unavailable — controls disabled",16,395);
    if (ui.snapshot().master) {
        const auto& master = *ui.snapshot().master;
        drawControls(master.key,master.muted,master.writable,master.pending,volumePercent(master),Interaction::masterY);
    }
    cv.color(.55,.65,.76); cv.text(32,207,"APPLICATION PLAYBACK",16);
    cv.color(.12,.16,.22); cv.box(24,212,952,368);
    auto& streams = ui.visibleStreams();
    if (streams.empty()) {
        cv.color(.8,.83,.9); cv.text(55,272, ui.snapshot().connected ? "No playback streams. Start music or a game." : "Waiting for the user audio server...",24);
        cv.text(55,312,"Only audio streams exposed on this headset can be mixed.",20);
    }
    for (int row=0; row<Interaction::rows; ++row) {
        auto index = ui.offset()+row;
        if (index >= streams.size()) break;
        const auto& s = streams[index];
        int top = Interaction::rowY + row*Interaction::rowHeight;
        cv.color(row%2 ? .10 : .13, row%2 ? .14 : .17, row%2 ? .19 : .23); cv.box(30,top,940,72);
        cv.color(.94,.96,1); cv.text(48,top+29,s.application,24,395);
        cv.color(.57,.64,.73); cv.text(48,top+56,(s.corked ? "Paused · " : "") + s.media + " [" + std::to_string(s.key.index)+"]",16,395);
        drawControls(s.key,s.muted,s.writable,s.pending,volumePercent(s),top);
    }
    cv.color(.9,.65,.42);
    if (!ui.snapshot().error.empty()) cv.text(32,609,ui.snapshot().error,17);
    else { cv.color(.58,.66,.75); cv.text(32,609,"Quit resets apps to 100% · Master volume unchanged",17); }
    const std::vector<std::pair<int,std::string>> buttons{{32,"Previous"},{185,"Next"},{355,ui.showInternals() ? "Internals: on" : "Internals: off"},{645,"Reconnect"},{812,confirmQuit ? "Quit now?" : "Quit"}};
    for (const auto& [x,label] : buttons) {
        cv.color(.16,.23,.32); cv.box(x,630,145,48); cv.color(.95,.97,1); cv.text(x+12,661,label,x == 355 ? 18 : 20,125);
    }
    cv.color(.7,.77,.84); cv.text(510,660,streams.empty() ? "0 apps" : std::to_string(ui.offset()+1)+"–"+std::to_string(std::min(ui.offset()+Interaction::rows,streams.size()))+" / "+std::to_string(streams.size()),17,125);
    return cv.pixels();
}
std::vector<std::uint8_t> renderIcon(int size) {
    Canvas cv(size,size); cv.color(.055,.075,.11); cv.box(0,0,size,size);
    for (int n=0; n<3; ++n) {
        double x=size*(.25+n*.25), y=size*(n==1 ? .35 : .65);
        cv.color(.25,.78,.87); cv.box(x-size*.025,size*.18,size*.05,size*.64);
        cv.color(.94,.96,1); cv.box(x-size*.10,y-size*.06,size*.20,size*.12);
    }
    return cv.pixels();
}
void writePpm(const std::string& path,const std::vector<std::uint8_t>& rgba,int width,int height) {
    if (rgba.size() != static_cast<std::size_t>(width)*height*4) throw std::runtime_error("Invalid screenshot dimensions");
    std::ofstream out(path,std::ios::binary); out << "P6\n" << width << ' ' << height << "\n255\n";
    for (std::size_t i=0; i<rgba.size(); i+=4) out.write(reinterpret_cast<const char*>(&rgba[i]),3);
    if (!out) throw std::runtime_error("Cannot write screenshot");
}
}
