// The avatar editor's kit and renderer: loading (and refusing damaged kits),
// looks and their file, every catalogue item resolving to kit parts, poses,
// and drawing (coverage, framing, colours, threads giving the same picture).
// Catalog (version 2) kits are tested on a small kit made here: categories from
// the catalog, items per body, hidden skin, the face composite, face shapes,
// see-through parts, the look file, and damaged copies.
// Run from bigapp (make test) so assets/avatar/kit.nxak is found.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "../avatar_kit.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

static int failures = 0, checks = 0;
#define CHECK(cond)                                                                                                    \
    do                                                                                                                 \
    {                                                                                                                  \
        ++checks;                                                                                                      \
        if (!(cond))                                                                                                   \
        {                                                                                                              \
            ++failures;                                                                                                \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);                                                \
        }                                                                                                              \
    } while (0)

using namespace avkit;

static std::vector<std::uint8_t> read_all(const char *path)
{
    std::vector<std::uint8_t> data;
    if (FILE *f = std::fopen(path, "rb"))
    {
        std::uint8_t buf[1 << 16];
        std::size_t n;
        while ((n = std::fread(buf, 1, sizeof buf, f)) > 0)
            data.insert(data.end(), buf, buf + n);
        std::fclose(f);
    }
    return data;
}

struct Box
{
    int x0 = 1 << 30, y0 = 1 << 30, x1 = -1, y1 = -1;
    long covered = 0;
};
static Box coverage(const Image &img)
{
    Box b;
    for (int y = 0; y < img.h; ++y)
        for (int x = 0; x < img.w; ++x)
            if (img.argb[std::size_t(y) * std::size_t(img.w) + std::size_t(x)] >> 24)
            {
                ++b.covered;
                b.x0 = std::min(b.x0, x);
                b.x1 = std::max(b.x1, x);
                b.y0 = std::min(b.y0, y);
                b.y1 = std::max(b.y1, y);
            }
    return b;
}

// Average colour of covered pixels in a rectangle of the picture.
static void average(const Image &img, int x0, int y0, int x1, int y1, int &r, int &g, int &b)
{
    long sr = 0, sg = 0, sb = 0, n = 0;
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x)
        {
            const std::uint32_t p = img.argb[std::size_t(y) * std::size_t(img.w) + std::size_t(x)];
            if ((p >> 24) < 250)
                continue;
            sr += (p >> 16) & 255;
            sg += (p >> 8) & 255;
            sb += p & 255;
            ++n;
        }
    r = n ? int(sr / n) : -1;
    g = n ? int(sg / n) : -1;
    b = n ? int(sb / n) : -1;
}

static void looks()
{
    Look l;
    l.body = 1;
    l.top = 3;
    l.top_colour = 12;
    l.glasses = 2;
    const Look back = parse_look(save_look(l));
    CHECK(back == l);
    // Out-of-range values are clamped, unknown keys and comments ignored.
    const Look odd = parse_look("# hello\nbody_type=7\nhairstyle=-3\nnonsense=4\ntop_colour=999\n");
    CHECK(odd.body == 1 && odd.hair == 0 && odd.top_colour == count_of(kCloth) - 1);
    // Stepping wraps both ways.
    const auto &cats = categories();
    int top = -1;
    for (std::size_t i = 0; i < cats.size(); ++i)
        if (std::string(cats[i].label) == "Top")
            top = int(i);
    CHECK(top >= 0);
    Look s;
    s.top = 0;
    step(s, top, -1);
    CHECK(s.top == count_of(kTops) - 1);
    step(s, top, 1);
    CHECK(s.top == 0);
    CHECK(choice_label(s, top) == "None");
    // An outfit piece's first colour keeps its own colours.
    s.top = 5;
    s.top_colour = 0;
    CHECK(choice_label(s, top + 1) == "Original");
    s.top = 2;
    CHECK(choice_label(s, top + 1) == "Charcoal");
    Colour c{};
    CHECK(choice_swatch(top + 1, 3, c) && c.r == kCloth[3].r);
    CHECK(!choice_swatch(top, 0, c));
    // Random looks are wearable and repeat for a seed.
    for (std::uint32_t seed = 1; seed < 200; ++seed)
    {
        Look r = random_look(seed);
        Look clamped = r;
        clamp(clamped);
        CHECK(r == clamped);
        CHECK(r.body == 0 || r.beard == 0);
    }
    CHECK(random_look(42) == random_look(42));
    CHECK(random_look(42) != random_look(43));
}

static void damaged(const std::vector<std::uint8_t> &good)
{
    Kit k;
    CHECK(!k.load(good.data(), 7) && k.empty());
    std::vector<std::uint8_t> bad = good;
    bad[0] = 'X';
    CHECK(!k.load(bad.data(), bad.size()) && k.empty());
    // Cut short anywhere: refused, never read past the end.
    for (std::size_t cut : {std::size_t(12), std::size_t(100), good.size() / 3, good.size() / 2, good.size() - 1})
    {
        std::vector<std::uint8_t> part(good.begin(), good.begin() + long(cut));
        CHECK(!k.load(part.data(), part.size()) && k.empty());
    }
}

static void kit_contents(const Kit &kit)
{
    CHECK(kit.joints.size() == 65 && kit.parent.size() == 65 && kit.parent[0] == -1);
    CHECK(kit.bodies.size() == 2 && kit.bodies[0].name == "male" && kit.bodies[1].name == "female");
    for (const Body &b : kit.bodies)
    {
        CHECK(b.rest.size() == 65 && b.top_y > 1.6f && b.top_y < 2.0f && b.brow_y < b.top_y);
        for (const char *clip : {"idle", "talk", "dance", "interact", "jump", "wave"})
        {
            const Clip *c = b.clip(clip);
            CHECK(c && c->name == clip && c->frames > 10 && c->pose.size() == std::size_t(c->frames) * 65);
        }
    }
    // Every catalogue item finds its parts on both bodies; textures are powers of two.
    for (int bi = 0; bi < 2; ++bi)
    {
        const std::string b = kit.bodies[std::size_t(bi)].name;
        auto has = [&](const char *spec) {
            std::string s = spec;
            std::size_t at = 0;
            while (at <= s.size())
            {
                std::size_t bar = s.find('|', at);
                if (bar == std::string::npos)
                    bar = s.size();
                std::string name = s.substr(at, bar - at);
                const std::size_t p = name.find("{b}");
                if (p != std::string::npos)
                    name.replace(p, 3, b);
                if (kit.part(name))
                    return true;
                at = bar + 1;
            }
            return false;
        };
        for (const auto *list : {kHairs, kBeards})
            (void)list;
        auto all = [&](const Item *items, int n) {
            for (int i = 0; i < n; ++i)
                for (const char *p : items[i].parts)
                    if (p)
                        CHECK(has(p));
        };
        all(kHairs, count_of(kHairs));
        all(kBeards, count_of(kBeards));
        all(kTops, count_of(kTops));
        all(kBottoms, count_of(kBottoms));
        all(kShoes, count_of(kShoes));
        all(kHats, count_of(kHats));
        all(kGlasses, count_of(kGlasses));
        CHECK(kit.part(b + "_body") && kit.part(b + "_eyes") && kit.part(b + "_brows"));
        const Part *body = kit.part(b + "_body");
        CHECK(body && !body->prims.empty() && body->prims[0].region.size() * 3 == body->prims[0].idx.size());
    }
    for (const Texture &t : kit.textures)
        CHECK(t.w > 0 && (t.w & (t.w - 1)) == 0 && (t.h & (t.h - 1)) == 0 && t.rgba.size() == std::size_t(t.w * t.h * 4));
}

static void drawing(const Kit &kit)
{
    Renderer r(kit);
    Look l;
    l.body = 0;
    l.top = 2;          // T-shirt
    l.top_colour = 3;   // red
    l.bottom = 0;       // trousers
    l.bottom_colour = 8; // blue
    r.set_look(l);
    // Poses: the idle loops; Avatar proportions make the head bigger.
    const auto p0 = r.pose("idle", 0), p1 = r.pose("idle", kit.bodies[0].clip("idle")->seconds());
    CHECK(p0.size() == 65 && p1.size() == 65);
    float diff = 0;
    for (std::size_t j = 0; j < 65; ++j)
        for (int k = 0; k < 12; ++k)
            diff = std::max(diff, std::fabs(p0[j].m[k] - p1[j].m[k]));
    CHECK(diff < 0.02f);
    const int head = kit.joint("Head");
    Look classic = l;
    classic.shape = 0;
    Renderer rc(kit);
    rc.set_look(classic);
    const auto pc = rc.pose("idle", 0);
    CHECK(std::fabs(p0[std::size_t(head)].m[0] * p0[std::size_t(head)].m[0] + p0[std::size_t(head)].m[4] * p0[std::size_t(head)].m[4] +
                    p0[std::size_t(head)].m[8] * p0[std::size_t(head)].m[8] -
                    kHeadScale * kHeadScale *
                        (pc[std::size_t(head)].m[0] * pc[std::size_t(head)].m[0] + pc[std::size_t(head)].m[4] * pc[std::size_t(head)].m[4] +
                         pc[std::size_t(head)].m[8] * pc[std::size_t(head)].m[8])) < 1e-3f);

    // Full body: covered, centred, standing in the frame.
    Image img;
    img.resize(200, 400);
    r.render(img, p0, Camera::full(), 2);
    const Box b = coverage(img);
    CHECK(b.covered > 8000 && b.covered < 60000);
    CHECK(b.y0 > 0 && b.y0 < 60 && b.y1 > 330 && b.y1 < 398); // head and feet both inside
    CHECK(std::abs((b.x0 + b.x1) / 2 - 100) < 20);
    // The T-shirt is red, the trousers blue.
    int cr, cg, cb;
    average(img, 85, 120, 115, 150, cr, cg, cb);
    CHECK(cr > 2 * cg && cr > 2 * cb);
    average(img, 70, 260, 130, 300, cr, cg, cb);
    CHECK(cb > 2 * cr);
    // The same picture with helper threads, band for band.
    Renderer rt(kit);
    rt.set_threads(3);
    rt.set_look(l);
    Image img2;
    img2.resize(200, 400);
    rt.render(img2, p0, Camera::full(), 2);
    CHECK(img2.argb == img.argb);
    rt.set_threads(0);
    // Turned round: a different picture of the same size of figure.
    Camera back = Camera::full();
    back.yaw = 3.14159f;
    Image img3;
    img3.resize(200, 400);
    r.render(img3, p0, back, 2);
    const Box b3 = coverage(img3);
    CHECK(img3.argb != img.argb && std::abs(b3.covered - b.covered) < b.covered / 4);
    // Head and shoulders fill the picture.
    Image head_img;
    head_img.resize(128, 128);
    r.render(head_img, p0, r.head_camera(), 2);
    CHECK(coverage(head_img).covered > 128 * 128 / 4);
    // No top: bare chest, so skin where the shirt was.
    Look bare = l;
    bare.top = 0;
    r.set_look(bare);
    r.render(img, p0, Camera::full(), 2);
    average(img, 85, 120, 115, 150, cr, cg, cb);
    CHECK(cr > cb && cg > cb && cr < 2 * cg + 40);
    // Every item of every category draws for both bodies without trouble.
    for (int body = 0; body < 2; ++body)
        for (std::size_t c = 0; c < categories().size(); ++c)
            for (int v = 0; v < categories()[c].count; ++v)
            {
                Look t;
                t.body = body;
                t.*(categories()[c].field) = v;
                r.set_look(t);
                Image small;
                small.resize(60, 120);
                r.render(small, r.pose("wave", 0.7f), Camera::full(), 1);
                CHECK(coverage(small).covered > 500);
            }
    // Nothing to draw into, or a pose of the wrong size: nothing happens.
    Image none;
    r.render(none, p0, Camera::full(), 2);
    CHECK(none.argb.empty());
    img.resize(20, 20);
    r.render(img, std::vector<M34>(3), Camera::full(), 2);
    CHECK(coverage(img).covered == 0);
}

// ---- a small version 2 (catalog) kit, written here --------------------------

struct Bytes
{
    std::vector<std::uint8_t> d;
    template <class T> void put(T v)
    {
        const auto *p = reinterpret_cast<const std::uint8_t *>(&v);
        d.insert(d.end(), p, p + sizeof v);
    }
    void str(const std::string &s)
    {
        put<std::uint8_t>(std::uint8_t(s.size()));
        d.insert(d.end(), s.begin(), s.end());
    }
    void ident(int n)
    {
        for (int i = 0; i < n; ++i)
            for (float f : {1.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f})
                put(f);
    }
    void tex(const char *name, int w, int h, std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a)
    {
        str(name);
        put<std::uint16_t>(std::uint16_t(w));
        put<std::uint16_t>(std::uint16_t(h));
        put(1.f), put(1.f), put(1.f);
        for (int i = 0; i < w * h; ++i)
            put(r), put(g), put(b), put(a);
    }
    // A square facing the camera, x0..x1 by y0..y1 at depth z.
    void quad(int tex, int tint, int flags, float x0, float x1, float y0, float y1, float z)
    {
        put<std::int16_t>(std::int16_t(tex));
        put<std::uint8_t>(std::uint8_t(tint));
        put<std::uint8_t>(std::uint8_t(flags));
        put<std::uint32_t>(4);
        const float c[4][4] = {{x0, y0, 0, 1}, {x1, y0, 1, 1}, {x1, y1, 1, 0}, {x0, y1, 0, 0}};
        for (const auto &v : c)
        {
            put(v[0]), put(v[1]), put(z), put(0.f), put(0.f), put(1.f), put(v[2]), put(v[3]);
            for (std::uint8_t j : {0, 0, 0, 0})
                put(j);
            for (std::uint8_t w : {255, 0, 0, 0})
                put(w);
        }
        put<std::uint32_t>(2);
        for (std::uint32_t i : {0u, 1u, 2u, 0u, 2u, 3u})
            put(i);
    }
};

// Parts: 0 masculine body, 1 feminine body, 2 head, 3 shirt, 4 glass.
// Textures: 0 skin (all tintable), 1 face base (clear), 2 red eye, 3 blue shirt, 4 white glass.
static std::vector<std::uint8_t> catalog_kit_bytes()
{
    Bytes k;
    k.d = {'N', 'X', 'A', 'K'};
    k.put<std::uint32_t>(2);
    k.put<std::uint32_t>(2); // joints
    k.str("ROOT");
    k.put<std::int16_t>(-1);
    k.str("HEAD");
    k.put<std::int16_t>(0);
    k.put<std::uint32_t>(5);
    k.tex("skin", 4, 4, 255, 255, 255, 255);
    k.tex("base", 4, 4, 0, 0, 0, 0);
    k.tex("eye", 2, 2, 255, 0, 0, 255);
    k.tex("shirt", 4, 4, 30, 60, 230, 0);
    k.tex("glass", 4, 4, 255, 255, 255, 128);
    k.put<std::uint32_t>(2); // bodies
    for (const char *b : {"male", "female"})
    {
        k.str(b);
        k.ident(2);
        k.put(1.6f), k.put(1.8f);
        k.put<std::uint32_t>(1);
        k.str("idle");
        k.put<std::uint16_t>(1);
        k.put(20.f);
        k.ident(2);
    }
    k.put<std::uint32_t>(5); // parts
    auto part = [&](const char *name, int body, int slot) {
        k.str(name);
        k.put<std::uint8_t>(std::uint8_t(body));
        k.put<std::uint8_t>(std::uint8_t(slot));
        k.ident(2);
        k.put<std::uint32_t>(1);
    };
    part("male_body", 0, SlotBody);
    k.quad(0, TintSkin, 1, -0.3f, 0.3f, 0.2f, 1.2f, 0);
    part("female_body", 1, SlotBody);
    k.quad(0, TintSkin, 1, -0.2f, 0.2f, 0.2f, 1.2f, 0);
    part("head", 255, SlotEyes);
    k.quad(-1, TintNone, 1 | 16, -0.25f, 0.25f, 1.3f, 1.75f, 0);
    part("shirt", 255, SlotTop);
    k.quad(3, TintNone, 1, -0.32f, 0.32f, 0.6f, 1.0f, 0.05f);
    part("glass", 255, SlotGlasses);
    k.quad(4, TintNone, 1 | 8, -0.25f, 0.25f, 1.45f, 1.6f, 0.2f);
    // faces: the head's eye channel covers every texel
    k.put<std::uint32_t>(1);
    for (std::uint16_t v : {2, 0, 4, 4})
        k.put(v);
    for (std::int16_t t : {1, -1, -1, -1, -1, -1})
        k.put(t);
    for (int i = 0; i < 6; ++i)
        k.put<std::uint8_t>(0);
    for (std::uint8_t h : {0, 0, 1, 0, 0})
        k.put(h);
    for (int i = 0; i < 16; ++i)
        k.put<std::int16_t>(4096), k.put<std::int16_t>(4096);
    // morphs: a big nose lifts the head's first corner
    k.put<std::uint32_t>(1);
    k.str("Big");
    k.put<std::uint16_t>(2);
    k.put<std::uint32_t>(1);
    k.put<std::uint16_t>(0), k.put<std::uint16_t>(0);
    for (float f : {-0.25f, 1.1f, 0.f, 0.f, 0.f, 1.f})
        k.put(f);
    // catalog
    k.put<std::uint32_t>(6);
    auto cat = [&](const char *label, int kind, int zoom, int arg, int items) {
        k.str(label);
        k.put<std::uint8_t>(std::uint8_t(kind));
        k.put<std::uint8_t>(std::uint8_t(zoom));
        k.put<std::uint8_t>(std::uint8_t(arg));
        k.put<std::uint16_t>(std::uint16_t(items));
    };
    cat("Body Type", KindBody, 0, 0, 2);
    k.str("Masculine"), k.put<std::uint8_t>(3);
    k.str("Feminine"), k.put<std::uint8_t>(3);
    cat("Skin Tone", KindColour, 1, SlotSkinTone, 2);
    k.str("Green"), k.put<std::uint8_t>(3), k.put<std::uint8_t>(0), k.put<std::uint8_t>(180), k.put<std::uint8_t>(0);
    k.str("Purple"), k.put<std::uint8_t>(3), k.put<std::uint8_t>(180), k.put<std::uint8_t>(0), k.put<std::uint8_t>(180);
    cat("Eyes", KindFace, 1, ChanEye, 2);
    k.str("Own"), k.put<std::uint8_t>(3), k.put<std::int16_t>(-1);
    k.str("Red"), k.put<std::uint8_t>(3), k.put<std::int16_t>(2);
    cat("Nose", KindMorph, 1, 0, 2);
    k.str("Default"), k.put<std::uint8_t>(3), k.put<std::int16_t>(-1);
    k.str("Big"), k.put<std::uint8_t>(3), k.put<std::int16_t>(0);
    auto wear = [&](const char *label, int bodies, std::uint32_t bits, int partn, bool hide) {
        k.str(label);
        k.put<std::uint8_t>(std::uint8_t(bodies));
        k.put(bits);
        k.put<std::uint16_t>(partn < 0 ? 0 : 1);
        if (partn >= 0)
            k.put<std::uint16_t>(std::uint16_t(partn));
        k.put<std::uint16_t>(0);
        k.put<std::uint16_t>(hide ? 1 : 0);
        if (hide)
        {
            k.put<std::uint8_t>(0), k.put<std::uint16_t>(0), k.put<std::uint32_t>(2);
            k.put<std::uint32_t>(0), k.put<std::uint32_t>(1);
        }
    };
    cat("Top", KindWear, 0, SlotTop, 3);
    wear("None", 3, 0, -1, false);
    wear("Shirt", 1, 8, 3, true);
    wear("Blouse", 2, 8, 3, false);
    cat("Glasses", KindWear, 1, SlotGlasses, 2);
    wear("None", 3, 0, -1, false);
    wear("Glass", 3, 0x100, 4, false);
    return k.d;
}

static long count_px(const Image &img, bool (*want)(int r, int g, int b))
{
    long n = 0;
    for (std::uint32_t p : img.argb)
        if ((p >> 24) > 200 && want(int((p >> 16) & 255), int((p >> 8) & 255), int(p & 255)))
            ++n;
    return n;
}

static void catalog_kits()
{
    const std::vector<std::uint8_t> bytes = catalog_kit_bytes();
    Kit kit;
    CHECK(kit.load(bytes.data(), bytes.size()));
    CHECK(kit.version == 2 && kit.catalog.size() == 6 && kit.faces.size() == 1 && kit.morphs.size() == 1);
    CHECK(kit.catalog[4].items[1].hides.size() == 1 && kit.catalog[4].items[1].hides[0].tris.size() == 2);
    // Every cut-short copy is refused without reading past the end.
    for (std::size_t n = 8; n < bytes.size(); n += 7)
    {
        Kit k2;
        CHECK(!k2.load(bytes.data(), n) && k2.empty());
    }

    use_kit(&kit);
    const auto &cats = categories();
    CHECK(cats.size() == 6 && std::string(cats[0].label) == "Body Type" && cats[4].count == 3 && cats[2].zoom);
    Look l = fresh_look();
    CHECK(l.pick.size() == 6 && value(l, cats[1]) == 1); // the middle swatch, clamped to the two there are
    CHECK(choice_label(l, 0) == "Masculine" && choice_label(l, 4) == "None");
    Colour c{};
    CHECK(choice_swatch(1, 0, c) && c.g == 180 && !choice_swatch(4, 0, c));
    // Tops step past the feminine-only blouse on a masculine body.
    step(l, 4, 1);
    CHECK(choice_label(l, 4) == "Shirt");
    step(l, 4, 1);
    CHECK(choice_label(l, 4) == "None");
    step(l, 4, -1);
    CHECK(choice_label(l, 4) == "Shirt");
    // A new body: the shirt is swapped for its first choice that fits.
    step(l, 0, 1);
    CHECK(choice_label(l, 0) == "Feminine" && choice_label(l, 4) == "None");
    step(l, 0, 1);
    // The look file, round trip.
    set_value(l, cats[2], 1);
    set_value(l, cats[4], 1);
    const Look back = parse_look(save_look(l));
    CHECK(back == l && choice_label(back, 2) == "Red");
    CHECK(random_look(9) == random_look(9));

    Renderer r(kit);
    Camera cam = Camera::full();
    auto draw = [&](const Look &look) {
        r.set_look(look);
        Image img;
        img.resize(200, 300);
        r.render(img, r.pose("idle", 0), cam, 1);
        return img;
    };
    auto green = [](int rr, int g, int b) { return g > 100 && rr < 60 && b < 60; };
    auto red = [](int rr, int g, int b) { return rr > 150 && g < 60 && b < 60; };
    auto grey = [](int rr, int g, int b) { return rr > 140 && std::abs(rr - g) < 12 && std::abs(g - b) < 12; };
    auto blue = [](int rr, int g, int b) { return b > 150 && rr < 90 && g < 120; };
    Look base = fresh_look();
    set_value(base, cats[1], 0); // green skin
    const Image plain = draw(base);
    CHECK(count_px(plain, green) > 2000 && count_px(plain, red) == 0 && count_px(plain, blue) == 0);
    // The face composite: the eye layer over the whole test head, in the iris
    // colour (its red channel says where the iris colour goes; 0.8 grey here).
    Look eyes = base;
    set_value(eyes, cats[2], 1);
    const Image red_face = draw(eyes);
    CHECK(count_px(red_face, grey) > 500 && count_px(plain, grey) == 0 && count_px(red_face, red) == 0);
    CHECK(count_px(red_face, green) < count_px(plain, green));
    // The shirt: blue, and the skin it hides is gone even where the shirt is not.
    Look shirt = base;
    set_value(shirt, cats[4], 1);
    const Image dressed = draw(shirt);
    CHECK(count_px(dressed, blue) > 500);
    CHECK(count_px(dressed, green) + count_px(dressed, blue) < count_px(plain, green));
    // A face shape moves the head.
    Look nose = base;
    set_value(nose, cats[3], 1);
    CHECK(draw(nose).argb != plain.argb);
    // See-through glass lightens what is behind it and hides nothing.
    Look glass = eyes;
    set_value(glass, cats[5], 1);
    const Image through = draw(glass);
    auto light = [](const Image &img) {
        long sum = 0;
        for (std::uint32_t p : img.argb)
            sum += long((p >> 16) & 255) + long((p >> 8) & 255) + long(p & 255);
        return sum;
    };
    CHECK(light(through) > light(red_face) + 20000);
    CHECK(count_px(through, grey) > 0 && coverage(through).covered == coverage(red_face).covered);
    // Purple skin tints the body.
    Look purple = base;
    set_value(purple, cats[1], 1);
    CHECK(count_px(draw(purple), [](int rr, int g, int b) { return rr > 100 && b > 100 && g < 40; }) > 2000);
    use_kit(nullptr);
    CHECK(categories().size() == 18 && categories()[0].field != nullptr);
}

int main(int argc, char **argv)
{
    const char *path = argc > 1 ? argv[1] : "assets/avatar/kit.nxak";
    const std::vector<std::uint8_t> data = read_all(path);
    Kit kit;
    CHECK(kit.load(data.data(), data.size()));
    looks();
    catalog_kits();
    if (!kit.empty())
    {
        damaged(data);
        kit_contents(kit);
        drawing(kit);
    }
    std::printf("%d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
