#include "hints.h"
#include "util/i18n.h"

#include <SDL.h>

#define HINTS_LEN 4
static const char *hints_list[HINTS_LEN] = {
        "Honeycomb-ing through the network to find a match.",
        "Layering your request—just like a perfect Ice Cream Sandwich.",
        "Jelly Bean-ing along—your connection is almost there.",
        "Taking a quick KitKat break while we sync.",
        "Lollipop-ing into existence... just a moment.",
        "Marshmallow-soft connection stability incoming.",
        "Nougat-ing to worry about, we’re just finalizing the link.",
        "Oreo-riginal connection handshake in progress.",
        "Pie-ing together the last few data packets.",
        "Quince-essential sync: Almost connected!",
        "Red Velvet smooth: Just finishing the final layer.",
        "Snow Cone-ing through the firewall to reach you.",
        "Tiramisu-ing the data over... almost ready!",
        "Upside Down Cake mode: Turning your connection right side up.",
        "Vanilla Ice Cream: Keeping it simple and fast."
};

const char *hints_obtain() {
    static int hint_idx = -1;
    if (hint_idx < 0) {
        hint_idx = (int) SDL_GetTicks() % HINTS_LEN;
    } else if (++hint_idx >= HINTS_LEN) {
        hint_idx = 0;
    }

    return locstr(hints_list[hint_idx]);
}