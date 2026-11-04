// Ex-Box 360: the avatar editor's kit and renderer. Pure (no SDL), header-only.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The kit (assets/avatar/kit.nxak, made by tools/build_avatar_kit.py from
// Quaternius's CC0 packs) holds two bodies on one 65-joint skeleton, hair,
// outfit pieces, our own glasses, and animations already fitted to each body.
// A Look picks a body, colours and clothes; the Renderer draws it with a small
// software rasteriser (skinning, z-buffer, per-vertex light, supersampling) into
// an ARGB image, so the avatar can change on the console without a GPU.
//
// Clothes come two ways: Quaternius's outfit pieces (with the skin under them
// hidden by body region), and clothes made from the body itself: the body's own
// triangles for chosen regions, pushed out along their normals and coloured
// (T-shirt, hoodie, trousers, shorts, sneakers, beanie). Those never clip,
// because they bend with exactly the same weights as the skin under them.
#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <new>
#include <string>
#include <utility>
#include <vector>
#include <cstdlib>
#if defined(__PROSPERO__) || defined(__unix__) || defined(__APPLE__)
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#ifdef __PROSPERO__
extern "C" std::int64_t sceKernelGetDirectMemorySize(void);
extern "C" int sceKernelAllocateDirectMemory(std::int64_t, std::int64_t, std::size_t,
                                                std::size_t, int, std::int64_t *);
extern "C" int sceKernelMapDirectMemory(void **, std::size_t, int, int,
                                           std::int64_t, std::size_t);
extern "C" int sceKernelMunmap(void *, std::size_t);
extern "C" int sceKernelReleaseDirectMemory(std::int64_t, std::size_t);
#endif

namespace avkit
{
#ifdef __PROSPERO__
inline std::atomic<std::size_t> direct_bytes_live{0};
inline std::atomic<std::size_t> direct_bytes_peak{0};
#endif
inline std::size_t direct_memory_live()
{
#ifdef __PROSPERO__
    return direct_bytes_live.load();
#else
    return 0;
#endif
}
// Store the large, persistent texture and animation arrays in the console's
// CPU-cached direct-memory pool. Other UI and temporary allocations continue
// to use the ordinary heap. The fallback retains the previous behaviour if a
// direct allocation is unavailable.
template <class T> struct DirectAllocator
{
    using value_type = T;
    DirectAllocator() noexcept = default;
    template <class U> DirectAllocator(const DirectAllocator<U> &) noexcept {}
    template <class U> struct rebind { using other = DirectAllocator<U>; };

    T *allocate(std::size_t n)
    {
        if (n > std::size_t(-1) / sizeof(T) - 64)
            throw std::bad_alloc();
        constexpr std::size_t header_size = 32;
        const std::size_t bytes = n * sizeof(T) + header_size;
#ifdef __PROSPERO__
        if (bytes >= (256u << 10))
        {
            const std::size_t mapped = (bytes + 0x3fff) & ~std::size_t(0x3fff);
            std::int64_t physical = -1;
            if (sceKernelAllocateDirectMemory(0, sceKernelGetDirectMemorySize(),
                                              mapped, 0x4000, 11, &physical) == 0)
            {
                void *base = nullptr;
                if (sceKernelMapDirectMemory(&base, mapped, 0x33, 0, physical, 0x4000) == 0 && base)
                {
                    auto *words = static_cast<std::uint64_t *>(base);
                    words[0] = 0x4e58454156444952ull; // NXEAVDIR
                    words[1] = mapped;
                    words[2] = static_cast<std::uint64_t>(physical);
                    const std::size_t live = direct_bytes_live.fetch_add(mapped) + mapped;
                    std::size_t peak = direct_bytes_peak.load();
                    while (live > peak && !direct_bytes_peak.compare_exchange_weak(peak, live)) {}
                    return reinterpret_cast<T *>(static_cast<unsigned char *>(base) + header_size);
                }
                sceKernelReleaseDirectMemory(physical, mapped);
            }
        }
#endif
        void *base = std::malloc(bytes);
        if (!base) throw std::bad_alloc();
        auto *words = static_cast<std::uint64_t *>(base);
        words[0] = 0;
        words[1] = bytes;
        words[2] = 0;
        return reinterpret_cast<T *>(static_cast<unsigned char *>(base) + header_size);
    }

    void deallocate(T *ptr, std::size_t) noexcept
    {
        if (!ptr) return;
        auto *base = reinterpret_cast<unsigned char *>(ptr) - 32;
        const auto *words = reinterpret_cast<const std::uint64_t *>(base);
#ifdef __PROSPERO__
        if (words[0] == 0x4e58454156444952ull)
        {
            const std::size_t mapped = static_cast<std::size_t>(words[1]);
            const std::int64_t physical = static_cast<std::int64_t>(words[2]);
            direct_bytes_live.fetch_sub(mapped);
            sceKernelMunmap(base, mapped);
            sceKernelReleaseDirectMemory(physical, mapped);
            return;
        }
#endif
        std::free(base);
    }
    template <class U> bool operator==(const DirectAllocator<U> &) const noexcept { return true; }
    template <class U> bool operator!=(const DirectAllocator<U> &) const noexcept { return false; }
};

// ---------------------------------------------------------------- the kit

enum Region
{
    HeadTop,
    Head,
    Neck,
    Chest,
    Belly,
    Hips,
    UpperArmA,
    UpperArmB,
    LowerArm,
    Hand,
    ThighA,
    ThighB,
    Calf,
    Foot,
    RegionCount
};
constexpr std::uint32_t bit(Region r)
{
    return 1u << r;
}

enum Slot
{
    SlotBody,
    SlotEyes,
    SlotBrows,
    SlotHair,
    SlotBeard,
    SlotTop,
    SlotSleeves,
    SlotLegs,
    SlotFeet,
    SlotHat,
    SlotGlasses,
    SlotExtra
};

enum Tint
{
    TintNone,
    TintSkin,
    TintHair,
    TintCloth,
    TintEyes
};

// A 3x4 affine matrix, row-major.
struct M34
{
    float m[12] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
};

inline M34 mul(const M34 &a, const M34 &b)
{
    M34 c;
    for (int r = 0; r < 3; ++r)
    {
        for (int k = 0; k < 3; ++k)
            c.m[r * 4 + k] = a.m[r * 4] * b.m[k] + a.m[r * 4 + 1] * b.m[4 + k] + a.m[r * 4 + 2] * b.m[8 + k];
        c.m[r * 4 + 3] = a.m[r * 4] * b.m[3] + a.m[r * 4 + 1] * b.m[7] + a.m[r * 4 + 2] * b.m[11] + a.m[r * 4 + 3];
    }
    return c;
}

struct Colour
{
    std::uint8_t r, g, b;
};

struct Texture
{
    std::string name;
    int w = 0, h = 0;
    float ref[3] = {1, 1, 1}; // average colour of the tintable texels
    std::vector<std::uint8_t, DirectAllocator<std::uint8_t>> rgba;
};

struct Clip
{
    std::string name;
    int frames = 0;
    float fps = 30;
    std::vector<M34, DirectAllocator<M34>> pose; // frames x joints, model space
    float seconds() const
    {
        return frames > 1 ? float(frames - 1) / fps : 0;
    }
};

struct Body
{
    std::string name;
    std::vector<M34> rest;
    float brow_y = 0, top_y = 0;
    std::vector<Clip> clips;
    const Clip *clip(const std::string &n) const
    {
        for (const auto &c : clips)
            if (c.name == n)
                return &c;
        return clips.empty() ? nullptr : &clips[0];
    }
};

#pragma pack(push, 1)
struct Vertex
{
    float p[3], n[3], uv[2];
    std::uint8_t j[4], w[4];
};
#pragma pack(pop)
static_assert(sizeof(Vertex) == 40, "kit vertex layout");

struct Prim
{
    int tex = -1, tint = TintNone, flags = 0; // flags: 1 double-sided, 2 has regions
    std::vector<Vertex> v;
    std::vector<std::uint32_t> idx;
    std::vector<std::uint8_t> region; // per triangle, for bodies
};

struct Part
{
    std::string name;
    int body = 255, slot = 0;
    std::vector<M34> ibm;
    std::vector<Prim> prims;
};

// Version 2 kits (a private kit built from your own 360's avatar items, see
// tools/build_avatar_kit360.py) also carry a face composite, face shapes and a
// catalog that the editor's categories come from.
struct FaceMap
{
    int part = 0, prim = 0, w = 0, h = 0;
    int tex[6] = {-1, -1, -1, -1, -1, -1}; // defaults: base, shadow, mouth, eye, facial hair, brow
    int flags[6] = {};                     // 1 wrap U, 2 wrap V, 4 clear outside 0..1
    std::vector<std::int16_t> map[5];      // per texel (u, v) * 8192 for each channel; -32768 none
};

struct Morph
{
    struct V
    {
        int prim, vtx;
        float p[3], n[3];
    };
    std::string name;
    int part = 0;
    std::vector<V> v;
};

enum CatalogKind
{
    KindWear,
    KindFace,
    KindMorph,
    KindColour,
    KindBody
};

// Face channels (FaceMap::tex order) and tint slots (the head's colours).
enum FaceChannel
{
    ChanBase,
    ChanShadow,
    ChanMouth,
    ChanEye,
    ChanFacialHair,
    ChanBrow
};
enum TintSlot
{
    SlotSkinTone,
    SlotHairColour,
    SlotLips,
    SlotIris,
    SlotBrowColour,
    SlotShadowColour,
    SlotFacialHairColour,
    TintSlots = 9
};

struct CatalogItem
{
    struct Hide
    {
        int body = 0, prim = 0;
        std::vector<std::uint32_t> tris;
    };
    std::string label;
    int bodies = 3;          // bit 0 masculine, bit 1 feminine
    std::uint32_t bits = 0;  // wear: what it covers (one item per bit)
    std::vector<int> parts;  // wear
    std::vector<int> hat;    // wear (hair): the parts to use under a hat
    std::vector<Hide> hides; // wear: body triangles it hides
    int ref = -1;            // face: texture; morph: morph
    Colour rgb{200, 200, 200};
};

struct CatalogCategory
{
    std::string label;
    int kind = KindWear, zoom = 0, arg = 0;
    std::vector<CatalogItem> items;
};

// Optional NXVP trailer: a selected catalog item may adjust the full avatar's
// stature and head proportions. Older kits have no trailer and use 1.0.
struct VisualProfile
{
    int category = 0, item = 0;
    float body_scale = 1.0f, head_scale = 1.0f;
    std::vector<std::pair<int, int>> choices; // other catalog values selected with this item
};

class Kit
{
  public:
    int version = 1;
    std::vector<std::string> joints;
    std::vector<int> parent;
    std::vector<Texture> textures;
    std::vector<Body> bodies;
    std::vector<Part> parts;
    std::vector<FaceMap> faces;
    std::vector<Morph> morphs;
    std::vector<CatalogCategory> catalog;
    std::vector<VisualProfile> visual_profiles;

    // Reads a whole kit file's bytes. False (and an empty kit) if it is not one.
    bool load(const std::uint8_t *data, std::size_t size)
    {
        p_ = data;
        end_ = data + size;
        ok_ = true;
        joints.clear();
        parent.clear();
        textures.clear();
        bodies.clear();
        parts.clear();
        faces.clear();
        morphs.clear();
        catalog.clear();
        visual_profiles.clear();
        if (size < 8 || std::memcmp(data, "NXAK", 4) != 0)
            return fail();
        p_ += 4;
        version = int(u32());
        if (version != 1 && version != 2)
            return fail();
        const std::uint32_t max_tex = version == 1 ? 256 : 8192, max_parts = version == 1 ? 1024 : 8192;
        const std::uint32_t nj = u32();
        if (nj == 0 || nj > 255)
            return fail();
        for (std::uint32_t i = 0; i < nj && ok_; ++i)
        {
            joints.push_back(str());
            parent.push_back(i16());
        }
        const std::uint32_t nt = u32();
        for (std::uint32_t i = 0; i < nt && ok_ && i < max_tex; ++i)
        {
            Texture t;
            t.name = str();
            t.w = u16();
            t.h = u16();
            for (float &f : t.ref)
                f = f32();
            const std::size_t bytes = std::size_t(t.w) * std::size_t(t.h) * 4;
            if (!take(bytes))
                break;
            t.rgba.assign(p_ - bytes, p_);
            textures.push_back(std::move(t));
        }
        const std::uint32_t nb = u32();
        for (std::uint32_t i = 0; i < nb && ok_ && i < 8; ++i)
        {
            Body b;
            b.name = str();
            b.rest = mats(nj);
            b.brow_y = f32();
            b.top_y = f32();
            const std::uint32_t nc = u32();
            if (nc > 256) return fail();
            for (std::uint32_t c = 0; c < nc && ok_; ++c)
            {
                Clip clip;
                clip.name = str();
                clip.frames = u16();
                clip.fps = f32();
                if (clip.fps <= 0 || clip.fps > 240)
                    return fail();
                const std::size_t poses = std::size_t(clip.frames) * nj;
                if (!take(poses * sizeof(M34))) break;
                clip.pose.resize(poses);
                std::memcpy(clip.pose.data(), p_ - poses * sizeof(M34), poses * sizeof(M34));
                b.clips.push_back(std::move(clip));
            }
            bodies.push_back(std::move(b));
        }
        const std::uint32_t np = u32();
        for (std::uint32_t i = 0; i < np && ok_ && i < max_parts; ++i)
        {
            Part part;
            part.name = str();
            part.body = u8();
            part.slot = u8();
            part.ibm = mats(nj);
            const std::uint32_t nprim = u32();
            for (std::uint32_t k = 0; k < nprim && ok_ && k < 64; ++k)
            {
                Prim pr;
                pr.tex = i16();
                pr.tint = u8();
                pr.flags = u8();
                const std::uint32_t nv = u32();
                if (!take(std::size_t(nv) * sizeof(Vertex)))
                    break;
                pr.v.resize(nv);
                std::memcpy(pr.v.data(), p_ - std::size_t(nv) * sizeof(Vertex), std::size_t(nv) * sizeof(Vertex));
                const std::uint32_t ntri = u32();
                if (!take(std::size_t(ntri) * 12))
                    break;
                pr.idx.resize(std::size_t(ntri) * 3);
                std::memcpy(pr.idx.data(), p_ - std::size_t(ntri) * 12, std::size_t(ntri) * 12);
                if (pr.flags & 2)
                {
                    if (!take(ntri))
                        break;
                    pr.region.assign(p_ - ntri, p_);
                }
                for (auto i3 : pr.idx)
                    if (i3 >= nv)
                        return fail();
                for (const auto &v : pr.v)
                    for (int q = 0; q < 4; ++q)
                        if (v.j[q] >= nj)
                            return fail();
                if (pr.tex >= int(textures.size()))
                    pr.tex = -1;
                part.prims.push_back(std::move(pr));
            }
            parts.push_back(std::move(part));
        }
        if (version >= 2 && ok_ && !load_v2())
            return fail();
        if (!ok_ || bodies.empty())
            return fail();
        return true;
    }

    // The face composite, face shapes and the catalog; checked against the parts.
    bool load_v2()
    {
        const std::uint32_t nf = u32();
        for (std::uint32_t i = 0; i < nf && ok_ && i < 16; ++i)
        {
            FaceMap f;
            f.part = u16();
            f.prim = u16();
            f.w = u16();
            f.h = u16();
            for (int &t : f.tex)
            {
                t = i16();
                if (t >= int(textures.size()))
                    t = -1;
            }
            for (int &fl : f.flags)
                fl = u8();
            int has[5];
            for (int &h : has)
                h = u8();
            if (f.part >= int(parts.size()) || f.prim >= int(parts[std::size_t(f.part)].prims.size()) || f.w <= 0 ||
                f.h <= 0 || f.w > 1024 || f.h > 1024)
                return false;
            const std::size_t n = std::size_t(f.w) * std::size_t(f.h) * 2;
            for (int c = 0; c < 5; ++c)
                if (has[c])
                {
                    if (!take(n * 2))
                        return false;
                    f.map[c].resize(n);
                    std::memcpy(f.map[c].data(), p_ - n * 2, n * 2);
                }
            faces.push_back(std::move(f));
        }
        const std::uint32_t nm = u32();
        for (std::uint32_t i = 0; i < nm && ok_ && i < 1024; ++i)
        {
            Morph m;
            m.name = str();
            m.part = u16();
            const std::uint32_t nv = u32();
            if (m.part >= int(parts.size()) || nv > 65536)
                return false;
            const Part &pt = parts[std::size_t(m.part)];
            for (std::uint32_t k = 0; k < nv && ok_; ++k)
            {
                Morph::V v;
                v.prim = u16();
                v.vtx = u16();
                for (float &f : v.p)
                    f = f32();
                for (float &f : v.n)
                    f = f32();
                if (v.prim >= int(pt.prims.size()) || v.vtx >= int(pt.prims[std::size_t(v.prim)].v.size()))
                    continue;
                m.v.push_back(v);
            }
            morphs.push_back(std::move(m));
        }
        const std::uint32_t nc = u32();
        for (std::uint32_t i = 0; i < nc && ok_ && i < 64; ++i)
        {
            CatalogCategory c;
            c.label = str();
            c.kind = u8();
            c.zoom = u8();
            c.arg = u8();
            const int ni = u16();
            for (int k = 0; k < ni && ok_; ++k)
            {
                CatalogItem it;
                it.label = str();
                it.bodies = u8();
                if (c.kind == KindWear)
                {
                    it.bits = u32();
                    for (int which = 0; which < 2; ++which)
                    {
                        const int n = u16();
                        for (int q = 0; q < n && ok_; ++q)
                        {
                            const int pi = u16();
                            if (pi < int(parts.size()))
                                (which ? it.hat : it.parts).push_back(pi);
                        }
                    }
                    const int nh = u16();
                    for (int q = 0; q < nh && ok_; ++q)
                    {
                        CatalogItem::Hide h;
                        h.body = u8();
                        h.prim = u16();
                        const std::uint32_t n = u32();
                        if (n > 65536 || !take(std::size_t(n) * 4))
                            return false;
                        h.tris.resize(n);
                        std::memcpy(h.tris.data(), p_ - std::size_t(n) * 4, std::size_t(n) * 4);
                        it.hides.push_back(std::move(h));
                    }
                }
                else if (c.kind == KindFace)
                {
                    it.ref = i16();
                    if (it.ref >= int(textures.size()))
                        it.ref = -1;
                }
                else if (c.kind == KindMorph)
                {
                    it.ref = i16();
                    if (it.ref >= int(morphs.size()))
                        it.ref = -1;
                }
                else if (c.kind == KindColour)
                {
                    it.rgb.r = std::uint8_t(u8());
                    it.rgb.g = std::uint8_t(u8());
                    it.rgb.b = std::uint8_t(u8());
                }
                c.items.push_back(std::move(it));
            }
            if (c.items.empty())
                continue;
            catalog.push_back(std::move(c));
        }
        if (!ok_ || p_ == end_)
            return ok_;
        if (!take(4) || std::memcmp(p_ - 4, "NXVP", 4) != 0)
            return false;
        const int nprofiles = u8();
        if (nprofiles > 32)
            return false;
        for (int i = 0; i < nprofiles && ok_; ++i)
        {
            VisualProfile profile;
            profile.category = u8();
            profile.item = u16();
            profile.body_scale = f32();
            profile.head_scale = f32();
            const int nchoices = u8();
            if (profile.category >= int(catalog.size()) ||
                profile.item >= int(catalog[std::size_t(profile.category)].items.size()) ||
                !std::isfinite(profile.body_scale) || !std::isfinite(profile.head_scale) ||
                profile.body_scale < 0.4f || profile.body_scale > 1.3f ||
                profile.head_scale < 0.7f || profile.head_scale > 1.4f || nchoices > 64)
                return false;
            for (int k = 0; k < nchoices && ok_; ++k)
            {
                const int cat = u8(), choice = u16();
                if (cat >= int(catalog.size()) || choice >= int(catalog[std::size_t(cat)].items.size()))
                    return false;
                profile.choices.emplace_back(cat, choice);
            }
            visual_profiles.push_back(profile);
        }
        return ok_ && p_ == end_;
    }

    bool load_file(const char *path)
    {
#if defined(__PROSPERO__) || defined(__unix__) || defined(__APPLE__)
        // A large private kit contains hundreds of MiB of animation matrices.
        // Reading it into a vector while load() copies its data can more than
        // double peak memory and exhaust a PS5 big app's flexible-memory cap.
        // File-backed pages are discarded as the parser advances.
        const int fd = open(path, O_RDONLY);
        if (fd < 0)
            return false;
        struct stat info{};
        if (fstat(fd, &info) != 0 || info.st_size < 8)
        {
            close(fd);
            return false;
        }
        const std::size_t size = static_cast<std::size_t>(info.st_size);
        void *mapped = mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
        close(fd);
        if (mapped == MAP_FAILED)
            return false;
        mapped_base_ = static_cast<const std::uint8_t *>(mapped);
        mapped_discarded_ = 0;
        const long page = sysconf(_SC_PAGESIZE);
        mapped_page_size_ = page > 0 ? static_cast<std::size_t>(page) : 4096;
        const bool ok = load(static_cast<const std::uint8_t *>(mapped), size);
        mapped_base_ = nullptr;
        munmap(mapped, size);
        p_ = end_ = nullptr;
        return ok;
#else
        FILE *f = std::fopen(path, "rb");
        if (!f)
            return false;
        std::vector<std::uint8_t> data;
        std::uint8_t buf[1 << 16];
        std::size_t n;
        while ((n = std::fread(buf, 1, sizeof buf, f)) > 0)
            data.insert(data.end(), buf, buf + n);
        std::fclose(f);
        return load(data.data(), data.size());
#endif
    }

    bool empty() const
    {
        return bodies.empty();
    }
    const Part *part(const std::string &name) const
    {
        for (const auto &p : parts)
            if (p.name == name)
                return &p;
        return nullptr;
    }
    int joint(const std::string &name) const
    {
        for (std::size_t i = 0; i < joints.size(); ++i)
            if (joints[i] == name)
                return int(i);
        return -1;
    }

  private:
    const std::uint8_t *p_ = nullptr, *end_ = nullptr;
#if defined(__PROSPERO__) || defined(__unix__) || defined(__APPLE__)
    const std::uint8_t *mapped_base_ = nullptr;
    std::size_t mapped_discarded_ = 0, mapped_page_size_ = 4096;
#endif
    bool ok_ = true;
    bool fail()
    {
        ok_ = false;
        joints.clear();
        parent.clear();
        textures.clear();
        bodies.clear();
        parts.clear();
        faces.clear();
        morphs.clear();
        catalog.clear();
        visual_profiles.clear();
        return false;
    }
    bool take(std::size_t n)
    {
#if defined(__PROSPERO__) || defined(__unix__) || defined(__APPLE__)
        // The previous field has been copied before the next take(). Evict its
        // file pages so a large kit does not keep both source and decoded data
        // resident at once. Keep the page containing p_ for tiny fields.
        if (mapped_base_ && p_ >= mapped_base_)
        {
            const std::size_t safe = (std::size_t(p_ - mapped_base_) / mapped_page_size_) * mapped_page_size_;
            if (safe >= mapped_discarded_ + (8u << 20))
            {
                madvise(const_cast<std::uint8_t *>(mapped_base_) + mapped_discarded_,
                        safe - mapped_discarded_, MADV_DONTNEED);
                mapped_discarded_ = safe;
            }
        }
#endif
        if (!ok_ || std::size_t(end_ - p_) < n)
        {
            ok_ = false;
            return false;
        }
        p_ += n;
        return true;
    }
    template <class T> T get()
    {
        T v{};
        if (take(sizeof(T)))
            std::memcpy(&v, p_ - sizeof(T), sizeof(T));
        return v;
    }
    std::uint32_t u32()
    {
        return get<std::uint32_t>();
    }
    int u16()
    {
        return get<std::uint16_t>();
    }
    int i16()
    {
        return get<std::int16_t>();
    }
    int u8()
    {
        return get<std::uint8_t>();
    }
    float f32()
    {
        return get<float>();
    }
    std::string str()
    {
        const int n = u8();
        if (!take(std::size_t(n)))
            return {};
        return std::string(reinterpret_cast<const char *>(p_ - n), std::size_t(n));
    }
    std::vector<M34> mats(std::size_t n)
    {
        std::vector<M34> out;
        if (!take(n * 48))
            return out;
        out.resize(n);
        std::memcpy(out.data(), p_ - n * 48, n * 48);
        return out;
    }
};

// ---------------------------------------------------------------- the look

// Palettes, in the spirit of the 360 editor's swatches.
constexpr Colour kSkin[] = {{255, 224, 196}, {241, 194, 160}, {224, 172, 130}, {198, 140, 98},
                            {166, 110, 74},  {132, 84, 56},   {98, 62, 42},    {70, 44, 32}};
constexpr Colour kHair[] = {{30, 26, 24},   {70, 48, 34},   {112, 76, 48},  {150, 108, 64}, {204, 168, 110}, {236, 214, 160},
                            {172, 64, 34},  {120, 120, 124}, {225, 225, 228}, {40, 70, 160},  {170, 50, 120},  {60, 150, 90}};
constexpr Colour kEyes[] = {{96, 62, 36}, {60, 40, 28}, {70, 110, 160}, {70, 130, 90}, {120, 110, 70}, {110, 110, 120}};
constexpr Colour kCloth[] = {{40, 44, 52},   {235, 235, 235}, {120, 124, 130}, {176, 36, 40},  {226, 104, 40}, {236, 196, 60},
                             {104, 176, 44}, {30, 110, 70},   {50, 120, 200},  {28, 48, 108},  {120, 70, 170}, {220, 110, 160},
                             {110, 74, 46},  {200, 176, 140}, {0, 0, 0},       {90, 150, 160}};
constexpr const char *kSkinNames[] = {"Porcelain", "Fair", "Light", "Medium", "Tan", "Brown", "Dark", "Deep"};
constexpr const char *kHairNames[] = {"Black", "Dark Brown", "Brown", "Light Brown", "Dark Blonde", "Blonde",
                                      "Auburn", "Grey", "White", "Blue", "Magenta", "Green"};
constexpr const char *kEyeNames[] = {"Brown", "Dark", "Blue", "Green", "Hazel", "Grey"};
constexpr const char *kClothNames[] = {"Charcoal", "White", "Grey", "Red", "Orange", "Yellow", "Lime", "Green",
                                       "Blue", "Navy", "Purple", "Pink", "Brown", "Tan", "Black", "Teal"};
template <class T, std::size_t N> constexpr int count_of(const T (&)[N])
{
    return int(N);
}

// Something to wear: kit parts ("{b}" is the body's name; "a|b" tries a then
// b), or clothes made from the body's own regions, or nothing.
// Where a garment made from the body ends, cut per pixel so hems are smooth:
// arm runs 0 at the shoulder, 1 at the elbow, 2 at the wrist; leg 0 at the hip,
// 1 at the knee, 2 at the ankle; heights are from a landmark (in metres).
enum Anchor
{
    AnyHeight,
    FromPelvis,
    FromNeck,
    FromBrow
};
struct Cut
{
    float arm_max = 9, leg_min = -9, leg_max = 9;
    int low = AnyHeight;
    float low_by = 0;
    int high = AnyHeight;
    float high_by = 0;
    bool neckline = false; // the upper height applies to the neck only (shoulders stay covered)
};

struct Item
{
    const char *label;
    const char *parts[3];
    std::uint32_t shell; // regions made into a garment
    float inflate;       // how far the garment stands off the skin (m)
    std::uint32_t hides; // skin regions hidden under it
    int hair;            // 0 leaves hair alone, 1 hides hair above the brow, 2 hides all hair
    bool textured;       // an outfit piece with its own texture: colour 0 keeps it as drawn
    Cut cut;
};

constexpr std::uint32_t kTorso = bit(Chest) | bit(Belly);
constexpr std::uint32_t kSleeveLong = bit(UpperArmA) | bit(UpperArmB) | bit(LowerArm);
constexpr std::uint32_t kShirt = kTorso | bit(Hips) | bit(ThighA) | bit(Neck) | kSleeveLong | bit(Hand);
constexpr std::uint32_t kLegs = bit(Belly) | bit(Hips) | bit(ThighA) | bit(ThighB) | bit(Calf) | bit(Foot);

constexpr Item kHairs[] = {
    {"Bald", {}, 0, 0, 0, 0, false, {}},
    {"Buzzed", {"hair_buzzed"}, 0, 0, 0, 0, false, {}},
    {"Soft Buzz", {"hair_buzzedfemale"}, 0, 0, 0, 0, false, {}},
    {"Side Part", {"hair_simpleparted"}, 0, 0, 0, 0, false, {}},
    {"Long", {"hair_long"}, 0, 0, 0, 0, false, {}},
    {"Buns", {"hair_buns"}, 0, 0, 0, 0, false, {}},
};
constexpr Item kBeards[] = {
    {"None", {}, 0, 0, 0, 0, false, {}},
    {"Beard", {"hair_beard"}, 0, 0, 0, 0, false, {}},
};
constexpr Item kTops[] = {
    {"None", {}, 0, 0, 0, 0, false, {}},
    {"Tank Top", {}, kShirt, 0.007f, 0, 0, false, {-0.12f, -9, 9, FromPelvis, 0.07f, FromNeck, -0.06f, true}},
    {"T-Shirt", {}, kShirt, 0.008f, 0, 0, false, {0.42f, -9, 9, FromPelvis, 0.06f, FromNeck, 0.015f, true}},
    {"Long Sleeve", {}, kShirt, 0.008f, 0, 0, false, {1.97f, -9, 9, FromPelvis, 0.06f, FromNeck, 0.02f, true}},
    {"Hoodie", {}, kShirt, 0.017f, 0, 0, false, {1.95f, -9, 9, FromPelvis, 0.03f, FromNeck, 0.05f, true}},
    {"Peasant Tunic", {"{b}_peasant_body", "{b}_peasant_arms"}, 0, 0, kTorso | kSleeveLong | bit(Hand), 0, true, {}},
    {"Ranger Jacket", {"{b}_ranger_body", "{b}_ranger_arms"}, 0, 0, kTorso | kSleeveLong | bit(Hand), 0, true, {}},
    {"Ranger + Pauldrons", {"{b}_ranger_body", "{b}_ranger_arms", "{b}_ranger_acc_pauldron|{b}_ranger_acc_pauldrons"},
     0, 0, kTorso | kSleeveLong | bit(Hand), 0, true, {}},
};
constexpr Item kBottoms[] = {
    {"Trousers", {}, kLegs, 0.006f, 0, 0, false, {9, -9, 1.93f, AnyHeight, 0, FromPelvis, 0.11f}},
    {"Shorts", {}, kLegs, 0.006f, 0, 0, false, {9, -9, 0.62f, AnyHeight, 0, FromPelvis, 0.11f}},
    {"Peasant Trousers", {"{b}_peasant_legs"}, 0, 0, bit(Hips) | bit(ThighA) | bit(ThighB), 0, true, {}},
    {"Ranger Trousers", {"{b}_ranger_legs"}, 0, 0, bit(Hips) | bit(ThighA) | bit(ThighB), 0, true, {}},
};
constexpr Item kShoes[] = {
    {"Sneakers", {}, bit(Foot) | bit(Calf), 0.011f, 0, 0, false, {9, 1.86f, 9, AnyHeight, 0, AnyHeight, 0}},
    {"Barefoot", {}, 0, 0, 0, 0, false, {}},
    {"Peasant Shoes", {"{b}_peasant_feet"}, 0, 0, bit(Foot), 0, true, {}},
    {"Ranger Boots", {"{b}_ranger_feet_boots|{b}_ranger_feet"}, 0, 0, bit(Foot) | bit(Calf), 0, true, {}},
};
constexpr Item kHats[] = {
    {"None", {}, 0, 0, 0, 0, false, {}},
    {"Beanie", {}, bit(HeadTop) | bit(Head), 0.03f, 0, 1, false, {9, -9, 9, FromBrow, 0.004f, AnyHeight, 0}},
    {"Ranger Hood", {"{b}_ranger_head_hood"}, 0, 0, 0, 2, true, {}},
};
constexpr Item kGlasses[] = {
    {"None", {}, 0, 0, 0, 0, false, {}},
    {"Round", {"{b}_glasses_round"}, 0, 0, 0, 0, false, {}},
    {"Square", {"{b}_glasses_square"}, 0, 0, 0, 0, false, {}},
};
constexpr const char *kPatterns[] = {"Plain", "Stripes", "Two-Tone"};
constexpr const char *kBodies[] = {"Masculine", "Feminine"};
constexpr const char *kShapes[] = {"Classic", "Avatar"}; // Avatar: a bigger head, like the 360's
constexpr float kHeadScale = 1.32f;

struct Look
{
    int body = 0, shape = 1, skin = 2, eyes = 0;
    int hair = 3, hair_colour = 1, beard = 0;
    int top = 2, top_colour = 8, pattern = 0;
    int bottom = 0, bottom_colour = 9;
    int shoes = 0, shoes_colour = 0;
    int hat = 0, hat_colour = 3;
    int glasses = 0, glasses_colour = 14;
    std::vector<int> pick; // a catalog kit's choices, one per category
    bool operator==(const Look &o) const
    {
        return body == o.body && shape == o.shape && skin == o.skin && eyes == o.eyes && hair == o.hair &&
               hair_colour == o.hair_colour && beard == o.beard && top == o.top && top_colour == o.top_colour &&
               pattern == o.pattern && bottom == o.bottom && bottom_colour == o.bottom_colour && shoes == o.shoes &&
               shoes_colour == o.shoes_colour && hat == o.hat && hat_colour == o.hat_colour && glasses == o.glasses &&
               glasses_colour == o.glasses_colour && pick == o.pick;
    }
    bool operator!=(const Look &o) const
    {
        return !(*this == o);
    }
};

// The editor's categories, in order: what each changes and how many choices.
// With a catalog kit in use (use_kit) they come from its catalog instead.
struct Category
{
    const char *label;
    int Look::*field;
    int count;
    bool zoom; // the camera moves in on the head for it
    int pick = -1;                          // catalog: the category's index
    const CatalogCategory *entry = nullptr; // catalog: what it holds
};

inline const Kit *&catalog_kit()
{
    static const Kit *kit = nullptr;
    return kit;
}

// Makes the editor's categories follow this kit's catalog (nullptr: the built-in list).
inline void use_kit(const Kit *kit)
{
    catalog_kit() = kit && !kit->catalog.empty() ? kit : nullptr;
}

inline const std::vector<Category> &categories()
{
    static const std::vector<Category> list = {
        {"Body Type", &Look::body, 2, false},
        {"Proportions", &Look::shape, 2, false},
        {"Skin Tone", &Look::skin, count_of(kSkin), true},
        {"Eye Colour", &Look::eyes, count_of(kEyes), true},
        {"Hairstyle", &Look::hair, count_of(kHairs), true},
        {"Hair Colour", &Look::hair_colour, count_of(kHair), true},
        {"Facial Hair", &Look::beard, count_of(kBeards), true},
        {"Top", &Look::top, count_of(kTops), false},
        {"Top Colour", &Look::top_colour, count_of(kCloth), false},
        {"Top Pattern", &Look::pattern, count_of(kPatterns), false},
        {"Bottoms", &Look::bottom, count_of(kBottoms), false},
        {"Bottoms Colour", &Look::bottom_colour, count_of(kCloth), false},
        {"Shoes", &Look::shoes, count_of(kShoes), false},
        {"Shoes Colour", &Look::shoes_colour, count_of(kCloth), false},
        {"Hat", &Look::hat, count_of(kHats), true},
        {"Hat Colour", &Look::hat_colour, count_of(kCloth), true},
        {"Glasses", &Look::glasses, count_of(kGlasses), true},
        {"Glasses Colour", &Look::glasses_colour, count_of(kCloth), true},
    };
    const Kit *kit = catalog_kit();
    if (!kit)
        return list;
    static const Kit *built = nullptr;
    static std::vector<CatalogCategory> const *built_from = nullptr;
    static std::vector<Category> cat;
    if (built != kit || built_from != &kit->catalog || cat.size() != kit->catalog.size())
    {
        cat.clear();
        for (std::size_t i = 0; i < kit->catalog.size(); ++i)
        {
            const CatalogCategory &c = kit->catalog[i];
            cat.push_back({c.label.c_str(), nullptr, int(c.items.size()), c.zoom != 0, int(i), &c});
        }
        built = kit;
        built_from = &kit->catalog;
    }
    return cat;
}

// A category's value in a look.
inline int value(const Look &look, const Category &c)
{
    if (c.field)
        return look.*(c.field);
    return c.pick >= 0 && std::size_t(c.pick) < look.pick.size() ? look.pick[std::size_t(c.pick)] : 0;
}
inline void set_value(Look &look, const Category &c, int v)
{
    if (c.field)
    {
        look.*(c.field) = v;
        return;
    }
    if (c.pick < 0)
        return;
    if (look.pick.size() <= std::size_t(c.pick))
        look.pick.resize(std::size_t(c.pick) + 1, 0);
    look.pick[std::size_t(c.pick)] = v;
    if (const Kit *kit = catalog_kit())
        for (const VisualProfile &profile : kit->visual_profiles)
            if (profile.category == c.pick && profile.item == v)
                for (const auto &choice : profile.choices)
                {
                    if (look.pick.size() <= std::size_t(choice.first))
                        look.pick.resize(std::size_t(choice.first) + 1, 0);
                    look.pick[std::size_t(choice.first)] = choice.second;
                }
}

// A catalog look's body (0 masculine, 1 feminine).
inline int catalog_body(const Look &look)
{
    for (const auto &c : categories())
        if (c.entry && c.entry->kind == KindBody)
            return value(look, c);
    return 0;
}
inline bool fits_body(const Category &c, int item, int body)
{
    return !c.entry || item < 0 || item >= int(c.entry->items.size()) ||
           (c.entry->items[std::size_t(item)].bodies & (1 << body)) != 0;
}

// The name of a category's current choice ("T-Shirt", "Navy").
inline std::string choice_label(const Look &look, int category)
{
    const auto &cats = categories();
    if (category < 0 || category >= int(cats.size()))
        return {};
    const Category &cat = cats[std::size_t(category)];
    const int v = value(look, cat);
    if (cat.entry)
        return v >= 0 && v < int(cat.entry->items.size()) ? cat.entry->items[std::size_t(v)].label : std::string();
    const std::string name = cat.label;
    if (name == "Body Type")
        return kBodies[v];
    if (name == "Proportions")
        return kShapes[v];
    if (name == "Skin Tone")
        return kSkinNames[v];
    if (name == "Eye Colour")
        return kEyeNames[v];
    if (name == "Hairstyle")
        return kHairs[v].label;
    if (name == "Hair Colour")
        return kHairNames[v];
    if (name == "Facial Hair")
        return kBeards[v].label;
    if (name == "Top")
        return kTops[v].label;
    if (name == "Top Pattern")
        return kPatterns[v];
    if (name == "Bottoms")
        return kBottoms[v].label;
    if (name == "Shoes")
        return kShoes[v].label;
    if (name == "Hat")
        return kHats[v].label;
    if (name == "Glasses")
        return kGlasses[v].label;
    // A colour for an outfit piece: the first swatch keeps it as Quaternius drew it.
    const bool textured = (name == "Top Colour" && kTops[look.top].textured) ||
                          (name == "Bottoms Colour" && kBottoms[look.bottom].textured) ||
                          (name == "Shoes Colour" && kShoes[look.shoes].textured) ||
                          (name == "Hat Colour" && kHats[look.hat].textured);
    if (textured && v == 0)
        return "Original";
    return kClothNames[v];
}

// The swatch a colour category shows for a value, or false for other categories.
inline bool choice_swatch(int category, int value, Colour &out)
{
    const auto &cats = categories();
    if (category < 0 || category >= int(cats.size()))
        return false;
    const Category &cat = cats[std::size_t(category)];
    if (cat.entry)
    {
        if (cat.entry->kind != KindColour || value < 0 || value >= int(cat.entry->items.size()))
            return false;
        out = cat.entry->items[std::size_t(value)].rgb;
        return true;
    }
    const std::string name = cat.label;
    if (name == "Skin Tone")
        out = kSkin[value];
    else if (name == "Eye Colour")
        out = kEyes[value];
    else if (name == "Hair Colour")
        out = kHair[value];
    else if (name.find("Colour") != std::string::npos)
        out = kCloth[value];
    else
        return false;
    return true;
}

// Catalog looks: every choice in range and made for the look's body (else its
// first choice that is, "None" where there is one).
inline void fit_catalog(Look &look)
{
    const auto &cats = categories();
    look.pick.resize(cats.size(), 0);
    const int body = std::clamp(catalog_body(look), 0, 1);
    for (const auto &c : cats)
    {
        int v = std::clamp(value(look, c), 0, c.count - 1);
        if (!fits_body(c, v, body))
        {
            v = 0;
            for (int i = 0; i < c.count; ++i)
                if (fits_body(c, i, body))
                {
                    v = i;
                    break;
                }
        }
        set_value(look, c, v);
    }
}

// Moves a category's choice by step, wrapping (and skipping catalog items made
// for the other body).
inline void step(Look &look, int category, int by)
{
    const auto &cats = categories();
    if (category < 0 || category >= int(cats.size()))
        return;
    const Category &c = cats[std::size_t(category)];
    int v = value(look, c);
    const int body = c.entry ? catalog_body(look) : 0;
    for (int tries = 0; tries < c.count; ++tries)
    {
        v = ((v + by) % c.count + c.count) % c.count;
        if (fits_body(c, v, body))
            break;
    }
    set_value(look, c, v);
    if (c.entry)
        fit_catalog(look); // a new body: the clothes follow
}

inline void clamp(Look &look)
{
    if (catalog_kit())
    {
        fit_catalog(look);
        return;
    }
    for (const auto &c : categories())
        if (c.field)
        {
            int &v = look.*(c.field);
            v = std::clamp(v, 0, c.count - 1);
        }
}

// Where a new catalog look starts: mid-tone colours, the first of the rest.
inline Look fresh_look()
{
    Look l;
    if (!catalog_kit())
        return l;
    for (const auto &c : categories())
    {
        int v = 0;
        if (c.entry->kind == KindColour)
            v = c.entry->arg == SlotSkinTone ? 3 : c.entry->arg == SlotHairColour || c.entry->arg == SlotBrowColour ||
                                                        c.entry->arg == SlotFacialHairColour
                                                    ? 1
                                                    : 0;
        set_value(l, c, std::min(v, c.count - 1));
    }
    fit_catalog(l);
    return l;
}

// A random, wearable look (X in the editor), from any seed.
inline Look random_look(std::uint32_t seed)
{
    auto next = [&](int n) {
        seed = seed * 1664525u + 1013904223u;
        std::uint32_t x = seed ^ (seed >> 16); // mixed, so neighbouring seeds differ
        x *= 0x7feb352du;
        x ^= x >> 15;
        x *= 0x846ca68bu;
        x ^= x >> 16;
        return int(x % std::uint32_t(n));
    };
    Look l = fresh_look();
    if (catalog_kit())
    {
        const auto &cats = categories();
        for (const auto &c : cats)
            if (c.entry->kind == KindBody)
                set_value(l, c, next(c.count));
        const int body = catalog_body(l);
        for (const auto &c : cats)
        {
            if (c.entry->kind == KindBody)
                continue;
            const bool optional = c.entry->items[0].label == "None";
            // Accessories, face paint and costumes now and then; the rest always.
            if (optional && next(4) > 0)
            {
                set_value(l, c, 0);
                continue;
            }
            int v = next(c.count);
            for (int tries = 0; tries < c.count && !fits_body(c, v, body); ++tries)
                v = (v + 1) % c.count;
            set_value(l, c, v);
        }
        fit_catalog(l);
        return l;
    }
    for (const auto &c : categories())
        l.*(c.field) = next(c.count);
    l.shape = 1;
    if (l.body == 1)
        l.beard = 0;
    if (next(3) > 0)
        l.hat = 0;
    if (next(3) > 0)
        l.glasses = 0;
    return l;
}

// "key=value" lines; unknown keys are ignored and values clamped.
inline std::string save_look(const Look &l)
{
    std::string out = "# Ex-Box 360 avatar\n";
    for (const auto &c : categories())
    {
        std::string key = c.label;
        for (char &ch : key)
            ch = ch == ' ' ? '_' : char(std::tolower(static_cast<unsigned char>(ch)));
        out += key + "=" + std::to_string(value(l, c)) + "\n";
    }
    return out;
}

inline Look parse_look(const std::string &text)
{
    Look l = fresh_look();
    std::size_t at = 0;
    while (at < text.size())
    {
        std::size_t nl = text.find('\n', at);
        if (nl == std::string::npos)
            nl = text.size();
        const std::string line = text.substr(at, nl - at);
        at = nl + 1;
        const std::size_t eq = line.find('=');
        if (eq == std::string::npos || line[0] == '#')
            continue;
        const std::string key = line.substr(0, eq);
        const int value = std::atoi(line.c_str() + eq + 1);
        for (const auto &c : categories())
        {
            std::string k = c.label;
            for (char &ch : k)
                ch = ch == ' ' ? '_' : char(std::tolower(static_cast<unsigned char>(ch)));
            if (k == key)
                set_value(l, c, value);
        }
    }
    clamp(l);
    return l;
}

// ---------------------------------------------------------------- drawing

struct Image
{
    int w = 0, h = 0;
    std::vector<std::uint32_t> argb; // straight alpha
    void resize(int width, int height)
    {
        w = width;
        h = height;
        argb.assign(std::size_t(w) * std::size_t(h), 0);
    }
};

// Where the camera looks: yaw turns the avatar (radians, 0 faces you);
// center_y and span (metres) frame it.
struct Camera
{
    float yaw = 0;
    float center_y = 0.95f;
    float span = 2.1f;
    static Camera full()
    {
        return {};
    }
    static Camera head(const Body &b)
    {
        Camera c;
        c.center_y = b.top_y - 0.17f;
        c.span = 0.52f;
        return c;
    }
};

struct Mip
{
    std::vector<int> w, h;
    std::vector<std::vector<std::uint32_t>> level;
};

class Renderer
{
  public:
    explicit Renderer(const Kit &kit) : kit_(kit)
    {
    }
    ~Renderer()
    {
        set_threads(0);
    }
    Renderer(const Renderer &) = delete;
    Renderer &operator=(const Renderer &) = delete;

    // Helper threads for drawing (each takes a band of the picture). They are
    // started once and kept; 0 draws on the calling thread alone.
    void set_threads(int helpers)
    {
        {
            std::lock_guard<std::mutex> g(pool_lock_);
            stop_ = true;
        }
        pool_go_.notify_all();
        for (auto &t : pool_)
            t.join();
        pool_.clear();
        stop_ = false;
        for (int i = 0; i < std::clamp(helpers, 0, 7); ++i)
            pool_.emplace_back([this, i] { helper(i + 1); });
    }

    const Body *body() const
    {
        return body_;
    }
    // Head and shoulders, allowing for the Avatar proportions' bigger head.
    Camera head_camera() const
    {
        Camera c;
        if (!body_)
            return c;
        int neck = kit_.joint("neck_01");
        if (neck < 0)
            neck = kit_.joint("NECK");
        const float neck_y = !custom_rig_ && neck >= 0 ? body_->rest[std::size_t(neck)].m[7] : body_->top_y - 0.3f;
        const float scale = look_.shape == 1 && kit_.catalog.empty() ? kHeadScale : 1.0f;
        const float top = neck_y + (body_->top_y - neck_y) * scale;
        c.center_y = top - (top - neck_y) * 0.62f;
        c.span = (top - neck_y) * 2.1f;
        return c;
    }
    const Look &look() const
    {
        return look_;
    }
    bool has_custom_rig() const { return custom_rig_; }
    bool has_front_trace() const
    {
        return front_trace_ != nullptr;
    }

    // Builds what to draw for a look: parts, garments, tinted textures.
    void set_look(const Look &in)
    {
        look_ = in;
        clamp(look_);
        baked_lighting_ = false;
        custom_rig_ = false;
        face_detail_ = nullptr;
        front_trace_ = nullptr;
        draws_.clear();
        if (kit_.bodies.empty())
            return;
        if (!kit_.catalog.empty())
        {
            set_catalog_look();
            return;
        }
        const int bi = std::min(look_.body, int(kit_.bodies.size()) - 1);
        body_ = &kit_.bodies[std::size_t(bi)];
        bname_ = body_->name;
        if (fields_body_ != bi)
            make_fields(bi);
        hide_ = 0;
        hair_rule_ = 0;
        const Item *worn[] = {&kTops[look_.top], &kBottoms[look_.bottom], &kShoes[look_.shoes], &kHats[look_.hat]};
        for (const Item *it : worn)
        {
            hide_ |= it->hides;
            hair_rule_ = std::max(hair_rule_, it->hair);
        }
        if (tinted_.size() > 24)
            tinted_.clear(); // kept between looks, so stepping through colours stays quick
        // The body, eyes and brows.
        add_part(bname_ + "_body", TintSkin, skin_colour(), 0, 0);
        add_part(bname_ + "_eyes", TintEyes, kEyes[look_.eyes], 0, 0);
        add_part(bname_ + "_brows", TintHair, kHair[look_.hair_colour], 0, 0);
        for (const char *p : kHairs[look_.hair].parts)
            if (p)
                add_part(p, TintHair, kHair[look_.hair_colour], 0, 0);
        for (const char *p : kBeards[look_.beard].parts)
            if (p)
                add_part(p, TintHair, kHair[look_.hair_colour], 0, 0);
        add_item(kTops[look_.top], kCloth[look_.top_colour], look_.top_colour, look_.pattern);
        add_item(kBottoms[look_.bottom], kCloth[look_.bottom_colour], look_.bottom_colour, 0);
        add_item(kShoes[look_.shoes], kCloth[look_.shoes_colour], look_.shoes_colour, 3);
        add_item(kHats[look_.hat], kCloth[look_.hat_colour], look_.hat_colour, 0);
        add_item(kGlasses[look_.glasses], kCloth[look_.glasses_colour], look_.glasses_colour, 0);
        // Outer layers first, so the skin under them fails the depth test before it is shaded.
        std::stable_sort(draws_.begin(), draws_.end(), [](const Draw &a, const Draw &b) { return a.order < b.order; });
    }

    // Joint transforms for a clip at time t (seconds; loops), with the look's proportions.
    std::vector<M34> pose(const std::string &clip_name, float t) const
    {
        std::vector<M34> g;
        if (!body_)
            return g;
        const Clip *clip = body_->clip(clip_name);
        const std::size_t nj = kit_.joints.size();
        if (!clip || clip->frames < 1 || clip->pose.size() < std::size_t(clip->frames) * nj)
            return body_->rest;
        float f = t * clip->fps;
        const float last = float(clip->frames - 1);
        if (last > 0)
            f = std::fmod(std::max(0.0f, f), last);
        else
            f = 0;
        const int a = std::min(int(f), clip->frames - 1), b = std::min(a + 1, clip->frames - 1);
        const float w = f - float(a);
        g.resize(nj);
        for (std::size_t j = 0; j < nj; ++j)
        {
            const M34 &x = clip->pose[std::size_t(a) * nj + j];
            const M34 &y = clip->pose[std::size_t(b) * nj + j];
            for (int k = 0; k < 12; ++k)
                g[j].m[k] = x.m[k] + (y.m[k] - x.m[k]) * w;
        }
        // Avatar proportions: the head a third bigger, scaled about the neck.
        if (look_.shape == 1 && kit_.catalog.empty())
        {
            const int head = kit_.joint("Head"), neck = kit_.joint("neck_01");
            if (head >= 0 && neck >= 0)
            {
                const float s = kHeadScale;
                M34 &h = g[std::size_t(head)];
                const float px = g[std::size_t(neck)].m[3], py = g[std::size_t(neck)].m[7], pz = g[std::size_t(neck)].m[11];
                for (int r = 0; r < 3; ++r)
                {
                    for (int c = 0; c < 3; ++c)
                        h.m[r * 4 + c] *= s;
                    const float pivot = r == 0 ? px : r == 1 ? py : pz;
                    h.m[r * 4 + 3] = pivot + (h.m[r * 4 + 3] - pivot) * s;
                }
            }
        }
        return g;
    }

    // Draws the look in a pose into out (its size is the picture's size).
    // Supersamples ss x ss per pixel for smooth edges.
    void render(Image &out, const std::vector<M34> &pose, const Camera &cam, int ss = 2)
    {
        if (out.w <= 0 || out.h <= 0)
            return;
        ss = std::clamp(ss, 1, 4);
        W_ = out.w * ss;
        H_ = out.h * ss;
        colour_.assign(std::size_t(W_) * std::size_t(H_), 0);
        depth_.assign(std::size_t(W_) * std::size_t(H_), 0.0f); // 1/z; 0 is empty
        if (pose.size() == kit_.joints.size() && body_)
        {
            setup_camera(cam);
            // Standalone meshes may extend beyond the ordinary body silhouette.
            // Fit their complete yaw envelope, so rotating does not change zoom.
            fit_standalone_ = baked_lighting_ && cam.span >= 1.6f && !front_trace_ && !face_detail_;
            required_distance_ = 0.0f;
            // Texels needed: about the picture's pixels per metre (the supersamples average the rest).
            const float want = float(out.h) / cam.span * 1.6f;
            for (Draw &d : draws_)
            {
                d.texture = nullptr;
                if (!d.mip || d.mip->level.empty())
                    continue;
                std::size_t l = 0;
                while (l + 1 < d.mip->level.size() && float(baked_lighting_ ? std::min(d.mip->w[l + 1], d.mip->h[l + 1]) : d.mip->w[l + 1]) >= want)
                    ++l;
                d.texture = d.mip->level[l].data();
                d.tw = d.mip->w[l];
                d.th = d.mip->h[l];
            }
            prepared_.resize(draws_.size());
            for (std::size_t i = 0; i < draws_.size(); ++i)
                prepare(draws_[i], prepared_[i], pose);
            if (fit_standalone_ && required_distance_ > dist_)
            {
                Camera framed = cam;
                framed.span = 2.0f * required_distance_ * tan_half_;
                setup_camera(framed);
                fit_standalone_ = false;
                for (std::size_t i = 0; i < draws_.size(); ++i)
                    prepare(draws_[i], prepared_[i], pose);
            }
            run_bands();
        }
        // Resolve: average each ss x ss block; alpha is the covered share.
        for (int y = 0; y < out.h; ++y)
            for (int x = 0; x < out.w; ++x)
            {
                unsigned r = 0, g = 0, b = 0, n = 0;
                for (int sy = 0; sy < ss; ++sy)
                    for (int sx = 0; sx < ss; ++sx)
                    {
                        const std::size_t i = std::size_t(y * ss + sy) * std::size_t(W_) + std::size_t(x * ss + sx);
                        if (depth_[i] > 0)
                        {
                            const std::uint32_t c = colour_[i];
                            r += (c >> 16) & 255;
                            g += (c >> 8) & 255;
                            b += c & 255;
                            ++n;
                        }
                    }
                std::uint32_t px = 0;
                if (n)
                    px = (std::uint32_t(255 * n / unsigned(ss * ss)) << 24) | ((r / n) << 16) | ((g / n) << 8) | (b / n);
                out.argb[std::size_t(y) * std::size_t(out.w) + std::size_t(x)] = px;
            }
        if (baked_lighting_)
        {
            // A few single-pixel dark flecks remain where the source image was
            // reconstructed into fragmented face UVs. Repair isolated flecks
            // inside skin-coloured areas while retaining connected glasses,
            // brows and lips.
            const std::vector<std::uint32_t> source = out.argb;
            auto skin_pixel = [](std::uint32_t c) {
                const int r = int((c >> 16) & 255), g = int((c >> 8) & 255), b = int(c & 255);
                return (c >> 24) > 220 && r > 115 && g > 78 && r * 100 > g * 112 && g * 100 > b * 108;
            };
            for (int y = 1; y + 1 < out.h; ++y)
                for (int x = 1; x + 1 < out.w; ++x)
                {
                    const std::size_t i = std::size_t(y) * std::size_t(out.w) + std::size_t(x);
                    const std::uint32_t here = source[i];
                    if ((here >> 24) < 220 || skin_pixel(here))
                        continue;
                    int count = 0, dark = 0, sr = 0, sg = 0, sb = 0;
                    for (int dy = -1; dy <= 1; ++dy)
                        for (int dx = -1; dx <= 1; ++dx)
                        {
                            if (!dx && !dy) continue;
                            const std::uint32_t c = source[std::size_t(y + dy) * std::size_t(out.w) + std::size_t(x + dx)];
                            if (skin_pixel(c))
                            {
                                ++count; sr += int((c >> 16) & 255); sg += int((c >> 8) & 255); sb += int(c & 255);
                            }
                            else if ((c >> 24) > 220)
                                ++dark;
                        }
                    if (count < 6 || dark > 2) continue;
                    const int hr = int((here >> 16) & 255), hg = int((here >> 8) & 255);
                    if ((sr / count - hr) < 38 || (sg / count - hg) < 28) continue;
                    out.argb[i] = 0xFF000000u | (std::uint32_t(sr / count) << 16)
                                | (std::uint32_t(sg / count) << 8) | std::uint32_t(sb / count);
                }
        }
        if (face_detail_)
            apply_face_detail(out, cam);
        if (front_trace_)
            apply_front_trace(out, cam);
    }

    std::size_t triangles_drawn() const
    {
        return drawn_;
    }

  private:
    struct Draw
    {
        const Part *part = nullptr;
        const Prim *prim = nullptr;
        const Mip *mip = nullptr;               // tinted, packed RGB, halving sizes
        const std::uint32_t *texture = nullptr; // the level this render uses
        int tw = 0, th = 0;
        Colour colour{200, 200, 200}, second{200, 200, 200};
        int pattern = 0; // 0 plain, 1 stripes, 2 two-tone, 3 sole (shoes)
        float inflate = 0;
        std::uint32_t only = 0;  // draw just these regions (garments)
        std::uint32_t hide = 0;  // skip these regions (skin under clothes)
        float hair_above = 1e9f; // skip triangles above this height (hair under a hat)
        bool two_sided = false;
        bool cutting = false; // a garment made from the body: cut per pixel
        float arm_max = 9, leg_min = -9, leg_max = 9, y_low = -9, y_high = 9;
        bool neckline = false;
        int order = 9; // drawing order: outer layers first
        const std::vector<std::uint8_t> *tri_hide = nullptr; // per triangle: 1 hidden (under clothes)
        int alpha = 0; // 4: texels under half alpha are holes; 8: see-through (blended, drawn last)
    };

    static int order_of(int slot)
    {
        switch (slot)
        {
        case SlotGlasses: return 0;
        case SlotHat: return 1;
        case SlotHair:
        case SlotBeard: return 2;
        case SlotExtra: return 3;
        case SlotTop:
        case SlotSleeves: return 4;
        case SlotLegs: return 5;
        case SlotFeet: return 6;
        case SlotEyes:
        case SlotBrows: return 7;
        default: return 8;
        }
    }

    // Per body vertex: how far along the arm and the leg it is (see Cut).
    void make_fields(int bi)
    {
        fields_body_ = bi;
        arm_.clear();
        leg_.clear();
        const Body &b = kit_.bodies[std::size_t(bi)];
        const Part *part = kit_.part(b.name + "_body");
        auto y_of = [&](const char *j) {
            const int k = kit_.joint(j);
            return k >= 0 ? b.rest[std::size_t(k)].m[7] : 0.0f;
        };
        pelvis_y_ = y_of("pelvis");
        neck_y_ = y_of("neck_01");
        if (!part || part->prims.empty())
            return;
        const Prim &pr = part->prims[0];
        arm_.assign(pr.v.size(), -2);
        leg_.assign(pr.v.size(), -2);
        auto pos = [&](const std::string &j, float *out) {
            const int k = kit_.joint(j);
            if (k < 0)
                return false;
            out[0] = b.rest[std::size_t(k)].m[3];
            out[1] = b.rest[std::size_t(k)].m[7];
            out[2] = b.rest[std::size_t(k)].m[11];
            return true;
        };
        auto along = [](const float *p, const float *a, const float *c) {
            const float d[3] = {c[0] - a[0], c[1] - a[1], c[2] - a[2]};
            const float dd = d[0] * d[0] + d[1] * d[1] + d[2] * d[2];
            return dd > 0 ? ((p[0] - a[0]) * d[0] + (p[1] - a[1]) * d[1] + (p[2] - a[2]) * d[2]) / dd : 0.0f;
        };
        for (std::size_t i = 0; i < pr.v.size(); ++i)
        {
            const Vertex &v = pr.v[i];
            int dom = 0;
            for (int k = 1; k < 4; ++k)
                if (v.w[k] > v.w[dom])
                    dom = k;
            const std::string &jn = kit_.joints[v.j[dom]];
            const char side = jn.size() > 2 && jn[jn.size() - 2] == '_' ? jn.back() : 0;
            const std::string sfx = side ? std::string("_") + side : std::string();
            float s0[3], s1[3], s2[3];
            if (side && (jn.rfind("clavicle", 0) == 0 || jn.rfind("upperarm", 0) == 0 || jn.rfind("lowerarm", 0) == 0 ||
                         jn.rfind("hand", 0) == 0 || jn.find("_0") != std::string::npos))
            {
                if (pos("upperarm" + sfx, s0) && pos("lowerarm" + sfx, s1) && pos("hand" + sfx, s2))
                {
                    if (jn.rfind("lowerarm", 0) == 0)
                        arm_[i] = 1 + along(v.p, s1, s2);
                    else if (jn.rfind("clavicle", 0) == 0 || jn.rfind("upperarm", 0) == 0)
                        arm_[i] = along(v.p, s0, s1);
                    else
                        arm_[i] = 2 + std::max(0.0f, along(v.p, s1, s2) - 1);
                }
            }
            else if (side && (jn.rfind("thigh", 0) == 0 || jn.rfind("calf", 0) == 0 || jn.rfind("foot", 0) == 0 ||
                              jn.rfind("ball", 0) == 0))
            {
                if (pos("thigh" + sfx, s0) && pos("calf" + sfx, s1) && pos("foot" + sfx, s2))
                {
                    if (jn.rfind("thigh", 0) == 0)
                        leg_[i] = along(v.p, s0, s1);
                    else if (jn.rfind("calf", 0) == 0)
                        leg_[i] = 1 + along(v.p, s1, s2);
                    else
                        leg_[i] = 2 + std::max(0.0f, along(v.p, s1, s2) - 1);
                }
            }
            else if (jn == "pelvis" || jn == "root")
                leg_[i] = -0.5f;
            if (jn.rfind("spine", 0) == 0 || jn == "pelvis" || jn.rfind("neck", 0) == 0)
                arm_[i] = -1.0f;
        }
    }

    float landmark(int anchor) const
    {
        return anchor == FromPelvis ? pelvis_y_ : anchor == FromNeck ? neck_y_ : anchor == FromBrow && body_ ? body_->brow_y : 0.0f;
    }

    Colour skin_colour() const
    {
        return kSkin[look_.skin];
    }

    std::string resolve(const char *spec) const
    {
        std::string s = spec;
        std::size_t at = 0;
        while (at <= s.size())
        {
            std::size_t bar = s.find('|', at);
            if (bar == std::string::npos)
                bar = s.size();
            std::string name = s.substr(at, bar - at);
            const std::size_t b = name.find("{b}");
            if (b != std::string::npos)
                name.replace(b, 3, bname_);
            if (kit_.part(name))
                return name;
            at = bar + 1;
        }
        return {};
    }

    // A texture recoloured for this look, with smaller copies for small pictures;
    // cached by texture and colour.
    const Mip *tinted(int tex, int tint, Colour c, bool keep)
    {
        if (tex < 0 || tex >= int(kit_.textures.size()))
            return nullptr;
        const Texture &t = kit_.textures[std::size_t(tex)];
        const std::string key = std::to_string(tex) + ":" + std::to_string(tint) + ":" + std::to_string(c.r) + "," +
                                std::to_string(c.g) + "," + std::to_string(c.b) + (keep ? "k" : "");
        auto it = tinted_.find(key);
        if (it != tinted_.end())
            return &it->second;
        Mip &mip = tinted_[key];
        mip.level.emplace_back();
        mip.w.push_back(t.w);
        mip.h.push_back(t.h);
        std::vector<std::uint32_t> &px = mip.level.back();
        const std::size_t n = std::size_t(t.w) * std::size_t(t.h);
        px.resize(n);
        const float fr = c.r / 255.0f, fg = c.g / 255.0f, fb = c.b / 255.0f;
        const float ref_l = std::max(0.05f, 0.3f * t.ref[0] + 0.59f * t.ref[1] + 0.11f * t.ref[2]);
        for (std::size_t i = 0; i < n; ++i)
        {
            float r = t.rgba[i * 4] / 255.0f, g = t.rgba[i * 4 + 1] / 255.0f, b = t.rgba[i * 4 + 2] / 255.0f;
            const float a = t.rgba[i * 4 + 3] / 255.0f;
            if (!keep && tint != TintNone)
            {
                float nr = r, ng = g, nb = b;
                if (tint == TintSkin || tint == TintHair || tint == TintEyes)
                {
                    // Same shading, new colour: scale by target / the texture's own average.
                    nr = r * fr / std::max(0.05f, t.ref[0]);
                    ng = g * fg / std::max(0.05f, t.ref[1]);
                    nb = b * fb / std::max(0.05f, t.ref[2]);
                }
                else if (tint == TintCloth)
                {
                    // Dyed: the cloth's light and shade, in the new colour.
                    const float l = (0.3f * r + 0.59f * g + 0.11f * b) / ref_l;
                    nr = 0.15f * r + 0.85f * l * fr;
                    ng = 0.15f * g + 0.85f * l * fg;
                    nb = 0.15f * b + 0.85f * l * fb;
                }
                r += (nr - r) * a;
                g += (ng - g) * a;
                b += (nb - b) * a;
            }
            auto q = [](float v) { return std::uint32_t(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f); };
            px[i] = std::uint32_t(t.rgba[i * 4 + 3]) << 24 | q(r) << 16 | q(g) << 8 | q(b);
        }
        halve(mip);
        return &mip;
    }

    // Adds smaller copies down to 64 texels, averaging each 2 x 2 (alpha too).
    static void halve(Mip &mip)
    {
        // Halve down to 64 texels, averaging each 2 x 2.
        while (mip.w.back() >= 128 && mip.h.back() >= 128)
        {
            const int w = mip.w.back(), h = mip.h.back(), nw = w / 2, nh = h / 2;
            std::vector<std::uint32_t> next(std::size_t(nw) * std::size_t(nh));
            const std::vector<std::uint32_t> &src = mip.level.back();
            for (int y = 0; y < nh; ++y)
                for (int x = 0; x < nw; ++x)
                {
                    unsigned r = 0, g = 0, b = 0, a = 0;
                    for (int k = 0; k < 4; ++k)
                    {
                        const std::uint32_t q = src[std::size_t(y * 2 + k / 2) * std::size_t(w) + std::size_t(x * 2 + k % 2)];
                        a += q >> 24;
                        r += (q >> 16) & 255;
                        g += (q >> 8) & 255;
                        b += q & 255;
                    }
                    next[std::size_t(y) * std::size_t(nw) + std::size_t(x)] = (a / 4) << 24 | (r / 4) << 16 | (g / 4) << 8 | (b / 4);
                }
            mip.level.push_back(std::move(next));
            mip.w.push_back(nw);
            mip.h.push_back(nh);
        }
    }

    // ---- catalog kits (the private 360 kit) -----------------------------

    // Bilinear RGBA (0..1) from a kit texture, with the 360's sampler rules.
    void sample(const Texture &t, float u, float v, int flags, float out[4]) const
    {
        if ((flags & 4) && (u < 0 || u > 1 || v < 0 || v > 1))
        {
            out[0] = out[1] = out[2] = out[3] = 0;
            return;
        }
        if (!(flags & 1))
            u = std::clamp(u, 0.0f, 1.0f);
        if (!(flags & 2))
            v = std::clamp(v, 0.0f, 1.0f);
        const float fx = u * float(t.w) - 0.5f, fy = v * float(t.h) - 0.5f;
        const int x0 = int(std::floor(fx)), y0 = int(std::floor(fy));
        const float tx = fx - float(x0), ty = fy - float(y0);
        auto wrap = [](int i, int n, bool w) { return w ? ((i % n) + n) % n : std::clamp(i, 0, n - 1); };
        const int xa = wrap(x0, t.w, flags & 1), xb = wrap(x0 + 1, t.w, flags & 1);
        const int ya = wrap(y0, t.h, flags & 2), yb = wrap(y0 + 1, t.h, flags & 2);
        for (int c = 0; c < 4; ++c)
        {
            auto at = [&](int x, int y) { return t.rgba[(std::size_t(y) * std::size_t(t.w) + std::size_t(x)) * 4 + std::size_t(c)] / 255.0f; };
            const float top = at(xa, ya) + (at(xb, ya) - at(xa, ya)) * tx;
            const float bot = at(xa, yb) + (at(xb, yb) - at(xa, yb)) * tx;
            out[c] = top + (bot - top) * ty;
        }
    }

    // The face for one head primitive: skin, then eye shadow, mouth, eyes,
    // facial hair and brows over it, each in its colour (the 360 head shader).
    const Mip *face(const FaceMap &fm, const int tex[6], const float tint[TintSlots][3])
    {
        std::string key = "face:" + std::to_string(fm.part) + ":" + std::to_string(fm.prim);
        for (int i = 0; i < 6; ++i)
            key += ":" + std::to_string(tex[i]);
        for (int i = 0; i < TintSlots; ++i)
            for (int c = 0; c < 3; ++c)
                key += "," + std::to_string(int(tint[i][c] * 255.0f + 0.5f));
        auto it = faces_.find(key);
        if (it != faces_.end())
            return &it->second;
        Mip &mip = faces_[key];
        mip.w.push_back(fm.w);
        mip.h.push_back(fm.h);
        mip.level.emplace_back(std::size_t(fm.w) * std::size_t(fm.h));
        std::vector<std::uint32_t> &px = mip.level.back();
        const float *skin = tint[SlotSkinTone];
        // Which colour each channel takes (after base): shadow, mouth, eye, facial hair, brow.
        const int slot_of[5] = {SlotShadowColour, SlotLips, SlotIris, SlotFacialHairColour, SlotBrowColour};
        auto tex_at = [&](int ch) -> const Texture * {
            const int t = tex[ch];
            return t >= 0 && t < int(kit_.textures.size()) && kit_.textures[std::size_t(t)].w > 0 ? &kit_.textures[std::size_t(t)] : nullptr;
        };
        const Texture *base = tex_at(ChanBase);
        for (int y = 0; y < fm.h; ++y)
            for (int x = 0; x < fm.w; ++x)
            {
                const std::size_t i = std::size_t(y) * std::size_t(fm.w) + std::size_t(x);
                float rgb[3] = {skin[0], skin[1], skin[2]}, s[4];
                if (base)
                {
                    sample(*base, (float(x) + 0.5f) / float(fm.w), (float(y) + 0.5f) / float(fm.h), fm.flags[0], s);
                    for (int c = 0; c < 3; ++c)
                    {
                        const float mixed = skin[c] * s[0] + skin[c] * s[1] + skin[c] * s[2]; // features 1/2 are skin
                        rgb[c] += (mixed - rgb[c]) * s[3];
                    }
                }
                for (int ch = 0; ch < 5; ++ch)
                {
                    const Texture *t = tex_at(ch + 1);
                    if (!t || fm.map[ch].empty())
                        continue;
                    const std::int16_t mu = fm.map[ch][i * 2], mv = fm.map[ch][i * 2 + 1];
                    if (mu == -32768)
                        continue;
                    sample(*t, float(mu) / 8192.0f, float(mv) / 8192.0f, fm.flags[ch + 1], s);
                    if (s[3] <= 0)
                        continue;
                    const float *tc = tint[slot_of[ch]];
                    for (int c = 0; c < 3; ++c)
                    {
                        const float f = ch == 0 ? tc[c] * s[0] : tc[c] * s[0] + s[1] + skin[c] * s[2];
                        rgb[c] += (f - rgb[c]) * s[3];
                    }
                }
                auto q = [](float v) { return std::uint32_t(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f); };
                px[i] = 0xFF000000u | q(rgb[0]) << 16 | q(rgb[1]) << 8 | q(rgb[2]);
            }
        halve(mip);
        return &mip;
    }

    // A catalog look: the body, the head (its shapes and face), then what it wears.
    void set_catalog_look()
    {
        const auto &cats = categories();
        auto pick_of = [&](std::size_t ci) { return ci < look_.pick.size() ? look_.pick[ci] : 0; };
        body_scale_ = head_scale_ = 1.0f;
        for (const VisualProfile &profile : kit_.visual_profiles)
            if (pick_of(std::size_t(profile.category)) == profile.item)
            {
                body_scale_ = profile.body_scale;
                head_scale_ = profile.head_scale;
            }
        int bi = 0;
        float tint[TintSlots][3];
        for (auto &t : tint)
            t[0] = t[1] = t[2] = 0.8f;
        int feat[6] = {-2, -2, -2, -2, -2, -2}; // -2: the head's own
        std::vector<int> morphs;
        for (std::size_t ci = 0; ci < kit_.catalog.size(); ++ci)
        {
            const CatalogCategory &c = kit_.catalog[ci];
            const int v = std::clamp(pick_of(ci), 0, int(c.items.size()) - 1);
            const CatalogItem &it = c.items[std::size_t(v)];
            if (c.kind == KindBody)
                bi = v;
            else if (c.kind == KindColour && c.arg < TintSlots)
            {
                tint[c.arg][0] = it.rgb.r / 255.0f;
                tint[c.arg][1] = it.rgb.g / 255.0f;
                tint[c.arg][2] = it.rgb.b / 255.0f;
            }
            else if (c.kind == KindFace && c.arg < 6)
                feat[c.arg] = it.ref < 0 ? -2 : it.ref;
            else if (c.kind == KindMorph && it.ref >= 0)
                morphs.push_back(it.ref);
        }
        (void)cats;
        bi = std::clamp(bi, 0, int(kit_.bodies.size()) - 1);
        body_ = &kit_.bodies[std::size_t(bi)];
        bname_ = body_->name;
        const Colour skin{std::uint8_t(tint[SlotSkinTone][0] * 255 + 0.5f), std::uint8_t(tint[SlotSkinTone][1] * 255 + 0.5f),
                          std::uint8_t(tint[SlotSkinTone][2] * 255 + 0.5f)};
        const Colour hair{std::uint8_t(tint[SlotHairColour][0] * 255 + 0.5f), std::uint8_t(tint[SlotHairColour][1] * 255 + 0.5f),
                          std::uint8_t(tint[SlotHairColour][2] * 255 + 0.5f)};
        for (int k : {7, 8})
            for (int c = 0; c < 3; ++c)
                tint[k][c] = tint[SlotSkinTone][c];
        // Caches are trimmed before this look's draws point into them.
        if (tinted_.size() > 48)
            tinted_.clear();
        if (faces_.size() > 8)
            faces_.clear();

        // A reconstructed full character replaces the kit's body and face.
        // It is kept in its own category so ordinary clothing remains usable.
        for (std::size_t ci = 0; ci < kit_.catalog.size(); ++ci)
        {
            const CatalogCategory &c = kit_.catalog[ci];
            if (c.label != "Image Model" || c.kind != KindWear)
                continue;
            const int v = std::clamp(pick_of(ci), 0, int(c.items.size()) - 1);
            if (v == 0)
                continue;
            const CatalogItem &it = c.items[std::size_t(v)];
            if (!(it.bodies & (1 << bi)))
                continue;
            body_scale_ = head_scale_ = 1.0f;
            baked_lighting_ = true;
            // Full characters can supply their own bind skeleton and clips.
            if (!it.parts.empty() && it.parts[0] >= 0 && it.parts[0] < int(kit_.parts.size()))
            {
                const int rig = kit_.parts[std::size_t(it.parts[0])].body;
                if (rig >= 2 && rig < int(kit_.bodies.size()))
                {
                    body_ = &kit_.bodies[std::size_t(rig)];
                    bname_ = body_->name;
                    custom_rig_ = true;
                }
            }
            for (const Texture &texture : kit_.textures)
            {
                if ((texture.name == it.label + " face detail" ||
                     texture.name == "Bayonetta 1 face detail") && texture.w == 256 && texture.h == 256)
                    face_detail_ = &texture;
                if ((texture.name == it.label + " front trace" ||
                     texture.name == "Bayonetta 1 front trace") &&
                    (texture.w == 512 || texture.w == 1024) && texture.h == 1024)
                    front_trace_ = &texture;
            }
            for (int pi : it.parts)
                if (pi >= 0 && pi < int(kit_.parts.size()))
                    add_catalog_part(kit_.parts[std::size_t(pi)], skin, hair);
            return;
        }

        // What is worn: one item per covered bit, in catalog order (a costume before a top).
        std::vector<const CatalogItem *> worn;
        std::vector<int> worn_slot;
        std::uint32_t covered = 0;
        bool hat = false;
        for (std::size_t ci = 0; ci < kit_.catalog.size(); ++ci)
        {
            const CatalogCategory &c = kit_.catalog[ci];
            if (c.kind != KindWear)
                continue;
            const int v = std::clamp(pick_of(ci), 0, int(c.items.size()) - 1);
            const CatalogItem &it = c.items[std::size_t(v)];
            if (it.parts.empty() || !(it.bodies & (1 << bi)) || (it.bits & covered))
                continue;
            covered |= it.bits;
            worn.push_back(&it);
            worn_slot.push_back(c.arg);
            if (c.arg == SlotHat)
                hat = true;
        }

        // The body, less what the clothes cover.
        hidden_.clear();
        const Part *body_part = nullptr;
        for (const Part &p : kit_.parts)
            if (p.slot == SlotBody && p.body == bi)
            {
                body_part = &p;
                break;
            }
        if (body_part)
        {
            hidden_.resize(body_part->prims.size());
            for (std::size_t k = 0; k < body_part->prims.size(); ++k)
                hidden_[k].assign(body_part->prims[k].idx.size() / 3, 0);
            for (const CatalogItem *it : worn)
                for (const auto &h : it->hides)
                    if (h.body == bi && h.prim < int(hidden_.size()))
                        for (std::uint32_t t : h.tris)
                            if (t < hidden_[std::size_t(h.prim)].size())
                                hidden_[std::size_t(h.prim)][t] = 1;
            add_catalog_part(*body_part, skin, hair);
            for (std::size_t k = 0; k < draws_.size(); ++k)
                if (draws_[k].part == body_part)
                    draws_[k].tri_hide = &hidden_[std::size_t(&*draws_[k].prim - &body_part->prims[0])];
        }

        // The head: its face shapes, then the face drawn for each primitive.
        const Part *head = nullptr;
        for (const auto &fm : kit_.faces)
        {
            head = &kit_.parts[std::size_t(fm.part)];
            break;
        }
        if (head)
        {
            head_ = *head;
            for (int m : morphs)
                for (const auto &mv : kit_.morphs[std::size_t(m)].v)
                {
                    if (&kit_.parts[std::size_t(kit_.morphs[std::size_t(m)].part)] != head)
                        continue;
                    Vertex &v = head_.prims[std::size_t(mv.prim)].v[std::size_t(mv.vtx)];
                    std::memcpy(v.p, mv.p, sizeof v.p);
                    std::memcpy(v.n, mv.n, sizeof v.n);
                }
            for (const auto &fm : kit_.faces)
            {
                if (&kit_.parts[std::size_t(fm.part)] != head)
                    continue;
                int tex[6];
                for (int ch = 0; ch < 6; ++ch)
                    tex[ch] = feat[ch] == -2 ? fm.tex[ch] : feat[ch];
                Draw d;
                d.part = &head_;
                d.prim = &head_.prims[std::size_t(fm.prim)];
                d.mip = face(fm, tex, tint);
                d.order = order_of(head_.slot);
                draws_.push_back(d);
            }
        }

        // Clothes, hair (its hat version under a hat) and accessories.
        for (std::size_t k = 0; k < worn.size(); ++k)
        {
            const CatalogItem &it = *worn[k];
            const std::vector<int> &parts = worn_slot[k] == SlotHair && hat && !it.hat.empty() ? it.hat : it.parts;
            for (int pi : parts)
                add_catalog_part(kit_.parts[std::size_t(pi)], skin, hair);
        }
        std::stable_sort(draws_.begin(), draws_.end(), [](const Draw &a, const Draw &b) { return a.order < b.order; });
    }

    void add_catalog_part(const Part &part, Colour skin, Colour hair)
    {
        for (const Prim &pr : part.prims)
        {
            Draw d;
            d.part = &part;
            d.prim = &pr;
            const Colour c = pr.tint == TintSkin ? skin : pr.tint == TintHair ? hair : Colour{255, 255, 255};
            if (pr.tex >= 0)
                d.mip = tinted(pr.tex, pr.tint, c, false);
            d.colour = c;
            d.two_sided = (pr.flags & 1) != 0;
            d.alpha = pr.flags & 12;
            d.order = d.alpha == 8 ? 99 : order_of(part.slot);
            draws_.push_back(d);
        }
    }

    void add_part(const std::string &name, int tint, Colour colour, std::uint32_t only, float inflate)
    {
        const Part *part = kit_.part(name);
        if (!part)
            return;
        for (const Prim &pr : part->prims)
        {
            Draw d;
            d.part = part;
            d.prim = &pr;
            const int use = pr.tint == TintSkin ? TintSkin : tint;
            const Colour c = pr.tint == TintSkin ? skin_colour() : colour;
            if (pr.tex >= 0)
            {
                d.mip = tinted(pr.tex, use, c, false);
            }
            d.colour = c;
            d.only = only;
            d.inflate = inflate;
            d.two_sided = (pr.flags & 1) != 0;
            if (part->slot == SlotBody)
                d.hide = hide_;
            if (part->slot == SlotHair && hair_rule_ == 2)
                continue;
            if (part->slot == SlotHair && hair_rule_ == 1)
                d.hair_above = body_->brow_y + 0.005f;
            d.order = order_of(part->slot);
            draws_.push_back(d);
        }
    }

    void add_item(const Item &item, Colour colour, int colour_index, int pattern)
    {
        for (const char *spec : item.parts)
        {
            if (!spec)
                continue;
            const std::string name = resolve(spec);
            const Part *part = name.empty() ? nullptr : kit_.part(name);
            if (!part)
                continue;
            for (const Prim &pr : part->prims)
            {
                Draw d;
                d.part = part;
                d.prim = &pr;
                d.two_sided = (pr.flags & 1) != 0;
                const bool keep = item.textured && colour_index == 0;
                const int tint = pr.tint == TintSkin ? TintSkin : TintCloth;
                const Colour c = pr.tint == TintSkin ? skin_colour() : colour;
                if (pr.tex >= 0)
                {
                    d.mip = tinted(pr.tex, tint, c, keep && tint == TintCloth);
                }
                d.colour = c;
                d.order = order_of(part->slot);
                draws_.push_back(d);
            }
        }
        if (item.shell)
        {
            const Part *part = kit_.part(bname_ + "_body");
            if (!part)
                return;
            for (const Prim &pr : part->prims)
            {
                Draw d;
                d.part = part;
                d.prim = &pr;
                d.colour = colour;
                // Stripes and two-tone pair the colour with white, or charcoal on light colours.
                const int lum = colour.r * 3 + colour.g * 6 + colour.b;
                d.second = lum > 1500 ? Colour{40, 44, 52} : Colour{235, 235, 235};
                d.pattern = pattern;
                d.only = item.shell;
                d.inflate = item.inflate;
                d.cutting = true;
                d.arm_max = item.cut.arm_max;
                d.leg_min = item.cut.leg_min;
                d.leg_max = item.cut.leg_max;
                d.y_low = item.cut.low == AnyHeight ? -9.0f : landmark(item.cut.low) + item.cut.low_by;
                d.y_high = item.cut.high == AnyHeight ? 9.0f : landmark(item.cut.high) + item.cut.high_by;
                d.neckline = item.cut.neckline;
                // Shoes, bottoms, tops, hats: the order they sit over each other.
                d.order = pattern == 3 ? 6 : item.shell & bit(HeadTop) ? 1 : item.shell & bit(Chest) ? 4 : 5;
                draws_.push_back(d);
            }
        }
    }

    void setup_camera(const Camera &cam)
    {
        const float fov = 0.42f; // radians, vertical
        tan_half_ = std::tan(fov / 2);
        dist_ = cam.span / 2 / tan_half_;
        center_y_ = cam.center_y;
        focal_ = float(H_) / 2 / tan_half_;
        cy_ = std::cos(cam.yaw);
        sy_ = std::sin(cam.yaw);
    }

    struct Out
    {
        float x, y, iz; // screen position, 1/z
        float light;
        float u, v;
        float model_y;
        float arm, leg;
        bool ok;
    };

    struct Prepared
    {
        std::vector<Out> out;
        std::vector<std::uint32_t> tris; // the triangles to draw
    };

    // Skins and projects a draw's vertices and lists its triangles.
    void prepare(const Draw &d, Prepared &prep, const std::vector<M34> &pose)
    {
        const Prim &pr = *d.prim;
        std::vector<Out> &out_ = prep.out;
        prep.tris.clear();
        const std::size_t nj = kit_.joints.size();
        skin_.resize(nj);
        for (std::size_t j = 0; j < nj; ++j)
            skin_[j] = mul(pose[j], d.part->ibm[j]);
        // Which vertices the triangles we will draw use.
        const std::size_t nv = pr.v.size(), nt = pr.idx.size() / 3;
        use_.assign(nv, 0);
        keep_.assign(nt, 0);
        for (std::size_t t = 0; t < nt; ++t)
        {
            if (d.tri_hide && t < d.tri_hide->size() && (*d.tri_hide)[t])
                continue;
            if (!pr.region.empty())
            {
                const std::uint32_t r = 1u << pr.region[t];
                if (d.only && !(d.only & r))
                    continue;
                if (!d.only && (d.hide & r))
                    continue;
            }
            else if (d.only)
                continue;
            if (d.hair_above < 1e8f)
            {
                const auto &a = pr.v[pr.idx[t * 3]], &b = pr.v[pr.idx[t * 3 + 1]], &c = pr.v[pr.idx[t * 3 + 2]];
                if ((a.p[1] + b.p[1] + c.p[1]) / 3 > d.hair_above)
                    continue;
            }
            keep_[t] = 1;
            use_[pr.idx[t * 3]] = use_[pr.idx[t * 3 + 1]] = use_[pr.idx[t * 3 + 2]] = 1;
        }
        out_.resize(nv);
        // Studio light, fixed to the camera: key from the upper left, fill from the right, rim behind.
        const float kx = -0.45f, ky = 0.55f, kz = 0.70f;
        const float fx = 0.7f, fy = 0.1f, fz = 0.7f;
        for (std::size_t i = 0; i < nv; ++i)
        {
            if (!use_[i])
                continue;
            const Vertex &v = pr.v[i];
            float bx = v.p[0] + v.n[0] * d.inflate, by = v.p[1] + v.n[1] * d.inflate, bz = v.p[2] + v.n[2] * d.inflate;
            float px = 0, py = 0, pz = 0, nx = 0, ny = 0, nz = 0;
            for (int k = 0; k < 4; ++k)
            {
                if (!v.w[k])
                    continue;
                const float w = v.w[k] / 255.0f;
                const float *m = skin_[v.j[k]].m;
                px += w * (m[0] * bx + m[1] * by + m[2] * bz + m[3]);
                py += w * (m[4] * bx + m[5] * by + m[6] * bz + m[7]);
                pz += w * (m[8] * bx + m[9] * by + m[10] * bz + m[11]);
                nx += w * (m[0] * v.n[0] + m[1] * v.n[1] + m[2] * v.n[2]);
                ny += w * (m[4] * v.n[0] + m[5] * v.n[1] + m[6] * v.n[2]);
                nz += w * (m[8] * v.n[0] + m[9] * v.n[1] + m[10] * v.n[2]);
            }
            if (head_scale_ != 1.0f && (d.part == &head_ || d.part->slot == SlotHair || d.part->slot == SlotGlasses))
            {
                const int head_joint = kit_.joint("HEAD");
                if (head_joint >= 0)
                {
                    const M34 &head_pose = pose[std::size_t(head_joint)];
                    px = head_pose.m[3] + (px - head_pose.m[3]) * head_scale_;
                    py = head_pose.m[7] + (py - head_pose.m[7]) * head_scale_;
                    pz = head_pose.m[11] + (pz - head_pose.m[11]) * head_scale_;
                }
            }
            px *= body_scale_;
            py *= body_scale_;
            pz *= body_scale_;
            if (fit_standalone_)
            {
                const float radius = std::hypot(px, pz);
                const float safe_tan = tan_half_ * 0.92f;
                const float horizontal_tan = safe_tan * float(W_) / float(H_);
                const float horizontal = radius * std::sqrt(1.0f + 1.0f / (horizontal_tan * horizontal_tan));
                const float vertical = radius + std::fabs(py - center_y_) / safe_tan;
                required_distance_ = std::max(required_distance_, std::max(horizontal, vertical));
            }
            // Turn about the vertical axis, then view from the front.
            const float rx = cy_ * px + sy_ * pz, rz = -sy_ * px + cy_ * pz;
            const float rnx = cy_ * nx + sy_ * nz, rnz = -sy_ * nx + cy_ * nz;
            const float len = std::sqrt(rnx * rnx + ny * ny + rnz * rnz);
            const float ux = len > 0 ? rnx / len : 0, uy = len > 0 ? ny / len : 0, uz = len > 0 ? rnz / len : 1;
            const float z = dist_ - rz; // distance in front of the camera
            Out &o = out_[i];
            o.ok = z > 0.05f;
            if (!o.ok)
                continue;
            o.iz = 1.0f / z;
            o.x = float(W_) / 2 + rx * focal_ * o.iz;
            o.y = float(H_) / 2 - (py - center_y_) * focal_ * o.iz;
            float key = ux * kx + uy * ky + uz * kz, fill = ux * fx + uy * fy + uz * fz;
            if (d.two_sided)
            {
                key = std::fabs(key);
                fill = std::fabs(fill);
            }
            const float rim = 1.0f - std::fabs(uz);
            o.light = 0.42f + 0.62f * std::max(0.0f, key) + 0.18f * std::max(0.0f, fill) + 0.22f * rim * rim;
            o.u = v.uv[0];
            o.v = v.uv[1];
            o.model_y = v.p[1];
            const bool fields = d.cutting && arm_.size() == nv;
            o.arm = fields ? arm_[i] : 0;
            o.leg = fields ? leg_[i] : 0;
        }
        for (std::size_t t = 0; t < nt; ++t)
        {
            if (!keep_[t])
                continue;
            const Out &a = out_[pr.idx[t * 3]], &b = out_[pr.idx[t * 3 + 1]], &c = out_[pr.idx[t * 3 + 2]];
            if (a.ok && b.ok && c.ok)
                prep.tris.push_back(std::uint32_t(t));
        }
    }

    // Draws every prepared triangle's part inside rows [y_from, y_to).
    std::size_t band(int y_from, int y_to)
    {
        std::size_t n = 0;
        for (std::size_t i = 0; i < draws_.size(); ++i)
        {
            const Draw &d = draws_[i];
            const Prepared &prep = prepared_[i];
            const Prim &pr = *d.prim;
            for (std::uint32_t t : prep.tris)
            {
                const Out &a = prep.out[pr.idx[t * 3]], &b = prep.out[pr.idx[t * 3 + 1]], &c = prep.out[pr.idx[t * 3 + 2]];
                if (std::max({a.y, b.y, c.y}) < float(y_from) || std::min({a.y, b.y, c.y}) > float(y_to))
                    continue;
                n += raster(d, a, b, c, pr.region.empty() ? -1 : pr.region[t], y_from, y_to);
            }
        }
        return n;
    }

    void run_bands()
    {
        const int bands = int(pool_.size()) + 1;
        const int rows = (H_ + bands - 1) / bands;
        {
            std::lock_guard<std::mutex> g(pool_lock_);
            band_rows_ = rows;
            band_done_ = 0;
            band_count_.assign(std::size_t(bands), 0);
            ++generation_;
        }
        pool_go_.notify_all();
        const std::size_t mine = band(0, std::min(H_, rows));
        std::unique_lock<std::mutex> g(pool_lock_);
        band_count_[0] = mine;
        pool_done_.wait(g, [&] { return band_done_ == int(pool_.size()); });
        drawn_ = 0;
        for (std::size_t c : band_count_)
            drawn_ += c;
    }

    void helper(int index)
    {
        unsigned seen = 0;
        std::unique_lock<std::mutex> g(pool_lock_);
        for (;;)
        {
            pool_go_.wait(g, [&] { return stop_ || generation_ != seen; });
            if (stop_)
                return;
            seen = generation_;
            const int rows = band_rows_;
            g.unlock();
            const std::size_t n = band(std::min(H_, index * rows), std::min(H_, (index + 1) * rows));
            g.lock();
            band_count_[std::size_t(index)] = n;
            ++band_done_;
            pool_done_.notify_one();
        }
    }

    void apply_face_detail(Image &out, const Camera &cam) const
    {
        const float cy = std::cos(cam.yaw), sy = std::sin(cam.yaw);
        // The image and mesh have different inferred depth. Retire the front
        // detail quickly as the head turns to avoid doubled facial features.
        const float view = std::clamp((cy - .985f) / .015f, 0.0f, 1.0f);
        if (view <= 0 || !face_detail_ || face_detail_->rgba.size() != 256u * 256u * 4u)
            return;
        const float f = float(out.h) / (2.0f * std::tan(.42f / 2.0f));
        const float dist = cam.span / (2.0f * std::tan(.42f / 2.0f));
        auto project = [&](float x, float y, float z) {
            const float rx = cy * x + sy * z, rz = -sy * x + cy * z;
            const float inv = 1.0f / (dist - rz);
            return std::pair<float, float>{float(out.w) / 2 + rx * f * inv,
                                           float(out.h) / 2 - (y - cam.center_y) * f * inv};
        };
        // Aligned against the original concept and the model's actual full-body
        // camera. These coordinates describe a small 3D face-facing decal.
        const float left = project(-.021f, 1.48f, .37f).first;
        const float right = project(.245f, 1.18f, .37f).first;
        const float top = project(.11f, 1.48f, .37f).second;
        const float bottom = project(.11f, 1.18f, .37f).second;
        if (right <= left || bottom <= top)
            return;
        const int x0 = std::max(0, int(std::floor(left))), x1 = std::min(out.w - 1, int(std::ceil(right)));
        const int y0 = std::max(0, int(std::floor(top))), y1 = std::min(out.h - 1, int(std::ceil(bottom)));
        const auto &rgba = face_detail_->rgba;
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x)
            {
                const float u = (float(x) + .5f - left) / (right - left);
                const float v = (float(y) + .5f - top) / (bottom - top);
                if (u < 0 || u > 1 || v < 0 || v > 1)
                    continue;
                const std::size_t i = std::size_t(y) * std::size_t(out.w) + std::size_t(x);
                const std::uint32_t old = out.argb[i];
                if ((old >> 24) < 128)
                    continue;
                const float tx = u * 255, ty = v * 255;
                const int ix = std::min(254, int(tx)), iy = std::min(254, int(ty));
                const float wx = tx - float(ix), wy = ty - float(iy);
                auto channel = [&](int offset) {
                    const std::size_t j = (std::size_t(iy) * 256u + std::size_t(ix)) * 4u + std::size_t(offset);
                    const float a = float(rgba[j]) * (1 - wx) + float(rgba[j + 4]) * wx;
                    const float b = float(rgba[j + 1024]) * (1 - wx) + float(rgba[j + 1028]) * wx;
                    return a * (1 - wy) + b * wy;
                };
                const float alpha = channel(3) / 255.0f * view;
                if (alpha <= 0)
                    continue;
                auto blend = [&](int shift, int offset) {
                    return std::uint32_t(std::clamp(float((old >> shift) & 255) * (1 - alpha)
                                                    + channel(offset) * alpha, 0.0f, 255.0f));
                };
                out.argb[i] = (old & 0xFF000000u) | (blend(16, 0) << 16)
                            | (blend(8, 1) << 8) | blend(0, 2);
            }
    }

    void apply_front_trace(Image &out, const Camera &cam) const
    {
        // Head shots retain the separate face decal; this atlas is a full-body
        // reference and must not be squeezed into a head camera.
        if (cam.span < 1.6f || !front_trace_ ||
            front_trace_->rgba.size() != std::size_t(front_trace_->w) * std::size_t(front_trace_->h) * 4u)
            return;
        const float cy = std::cos(cam.yaw);
        // The inferred mesh differs in pose. Keep the handover within one
        // degree so two mismatched silhouettes do not remain visible together.
        float weight = std::clamp((cy - .99985f) / .00015f, 0.0f, 1.0f);
        weight = weight * weight * (3.0f - 2.0f * weight);
        if (weight <= 0)
            return;
        int left = out.w, top = out.h, right = -1, bottom = -1;
        for (int y = 0; y < out.h; ++y)
            for (int x = 0; x < out.w; ++x)
                if ((out.argb[std::size_t(y) * std::size_t(out.w) + std::size_t(x)] >> 24) > 128)
                {
                    left = std::min(left, x); right = std::max(right, x);
                    top = std::min(top, y); bottom = std::max(bottom, y);
                }
        if (right < left || bottom <= top)
            return;
        // Match the traced character's height and centre to the rendered 3D
        // figure. The transparent atlas preserves the source aspect.
        const float height = float(bottom - top + 1);
        const int tw = front_trace_->w, th = front_trace_->h;
        const float width = height * float(tw) / float(th);
        const float cx = float(left + right + 1) * .5f;
        const float x0 = cx - width * .5f;
        const auto &rgba = front_trace_->rgba;
        auto sample = [&](float tx, float ty, int channel) {
            tx = std::clamp(tx, 0.0f, float(tw - 1)); ty = std::clamp(ty, 0.0f, float(th - 1));
            const int ix = std::min(tw - 2, int(tx)), iy = std::min(th - 2, int(ty));
            const float fx = tx - float(ix), fy = ty - float(iy);
            const std::size_t i = (std::size_t(iy) * std::size_t(tw) + std::size_t(ix)) * 4u + std::size_t(channel);
            const std::size_t row = std::size_t(tw) * 4u;
            const float a = float(rgba[i]) * (1 - fx) + float(rgba[i + 4]) * fx;
            const float b = float(rgba[i + row]) * (1 - fx) + float(rgba[i + row + 4]) * fx;
            return a * (1 - fy) + b * fy;
        };
        for (int y = 0; y < out.h; ++y)
            for (int x = 0; x < out.w; ++x)
            {
                const std::size_t i = std::size_t(y) * std::size_t(out.w) + std::size_t(x);
                const std::uint32_t old = out.argb[i];
                const float old_alpha = float(old >> 24) / 255.0f;
                const float u = (float(x) + .5f - x0) / width;
                const float v = (float(y) + .5f - float(top)) / height;
                const bool inside = u >= 0 && u <= 1 && v >= 0 && v <= 1;
                if (!inside && old_alpha <= 0)
                    continue;
                const float new_alpha = inside ? sample(u * float(tw - 1), v * float(th - 1), 3) / 255.0f : 0.0f;
                const float a0 = old_alpha * (1.0f - weight), a1 = new_alpha * weight;
                const float alpha = a0 + a1;
                if (alpha <= 0)
                {
                    out.argb[i] = 0;
                    continue;
                }
                auto blend = [&](int shift, int channel) {
                    const float old_c = float((old >> shift) & 255);
                    const float new_c = inside ? sample(u * float(tw - 1), v * float(th - 1), channel) : 0.0f;
                    return std::uint32_t(std::clamp((old_c * a0 + new_c * a1) / alpha, 0.0f, 255.0f));
                };
                out.argb[i] = (std::uint32_t(std::clamp(alpha * 255.0f, 0.0f, 255.0f)) << 24)
                            | (blend(16, 0) << 16) | (blend(8, 1) << 8) | blend(0, 2);
            }
    }

    std::uint32_t shade(const Draw &d, float u, float v, float light, float model_y, float arm) const
    {
        float r, g, b;
        if (d.texture && d.tw > 0)
        {
            if (baked_lighting_)
            {
                // Smooth the many small UV islands in the reconstructed mesh.
                // Its atlas is padded, so adjacent taps remain on the same art.
                const float fx = u * float(d.tw) - .5f, fy = v * float(d.th) - .5f;
                const int x0 = int(std::floor(fx)), y0 = int(std::floor(fy));
                const float wx = fx - float(x0), wy = fy - float(y0);
                auto texel = [&](int x, int y) {
                    return d.texture[std::size_t(y & (d.th - 1)) * std::size_t(d.tw)
                                     + std::size_t(x & (d.tw - 1))];
                };
                const std::uint32_t c00 = texel(x0, y0), c10 = texel(x0 + 1, y0);
                const std::uint32_t c01 = texel(x0, y0 + 1), c11 = texel(x0 + 1, y0 + 1);
                auto channel = [&](int shift) {
                    const float a = float((c00 >> shift) & 255) * (1 - wx) + float((c10 >> shift) & 255) * wx;
                    const float b = float((c01 >> shift) & 255) * (1 - wx) + float((c11 >> shift) & 255) * wx;
                    return a * (1 - wy) + b * wy;
                };
                r = channel(16); g = channel(8); b = channel(0);
            }
            else
            {
                // Kit textures are powers of two: wrap with a mask.
                const int tx = int(u * float(d.tw)) & (d.tw - 1), ty = int(v * float(d.th)) & (d.th - 1);
                const std::uint32_t c = d.texture[std::size_t(ty) * std::size_t(d.tw) + std::size_t(tx)];
                r = float((c >> 16) & 255);
                g = float((c >> 8) & 255);
                b = float(c & 255);
            }
        }
        else
        {
            Colour c = d.colour;
            if (d.pattern == 1 && std::fmod(model_y * 22.0f + 100.0f, 2.0f) < 1.0f)
                c = d.second; // stripes, about 4.5 cm apart
            else if (d.pattern == 2 && arm > 0.06f)
                c = d.second; // two-tone: the sleeves
            else if (d.pattern == 3 && model_y < 0.028f)
                c = Colour{238, 238, 232}; // a white sole
            r = c.r;
            g = c.g;
            b = c.b;
        }
        // Imported game meshes opt into normal lighting with primitive flag 16.
        const float l = (baked_lighting_ && !(d.prim && (d.prim->flags & 16))) ? 1.0f : std::min(light, 1.35f);
        auto q = [](float x) { return std::uint32_t(std::clamp(x, 0.0f, 255.0f)); };
        return q(r * l) << 16 | q(g * l) << 8 | q(b * l);
    }

    std::size_t raster(const Draw &d, const Out &a, const Out &b, const Out &c, int region, int band_from, int band_to)
    {
        const float area = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
        if (std::fabs(area) < 1e-6f)
            return 0;
        // Clockwise on screen is facing away here (y grows downward).
        if (!d.two_sided && area > 0)
            return 0;
        const int x0 = std::max(0, int(std::floor(std::min({a.x, b.x, c.x}))));
        const int x1 = std::min(W_ - 1, int(std::ceil(std::max({a.x, b.x, c.x}))));
        const int y0 = std::max(band_from, int(std::floor(std::min({a.y, b.y, c.y}))));
        const int y1 = std::min(band_to - 1, int(std::ceil(std::max({a.y, b.y, c.y}))));
        if (x0 > x1 || y0 > y1)
            return 0;
        const float inv = 1.0f / area;
        // Barycentric weights as linear functions of x and y.
        const float e0x = -(c.y - b.y) * inv, e0y = (c.x - b.x) * inv;
        const float e1x = -(a.y - c.y) * inv, e1y = (a.x - c.x) * inv;
        const float e0c = ((b.x) * (c.y) - (b.y) * (c.x)) * inv;
        const float e1c = ((c.x) * (a.y) - (c.y) * (a.x)) * inv;
        // Attributes are interpolated straight across the screen: the avatar is
        // shallow next to its distance from the camera, so the error is tiny.
        const float du1 = b.u - a.u, du2 = c.u - a.u, dv1 = b.v - a.v, dv2 = c.v - a.v;
        const float dl1 = b.light - a.light, dl2 = c.light - a.light;
        const float dy1 = b.model_y - a.model_y, dy2 = c.model_y - a.model_y;
        const float da1 = b.arm - a.arm, da2 = c.arm - a.arm, dg1 = b.leg - a.leg, dg2 = c.leg - a.leg;
        const float dz1 = b.iz - a.iz, dz2 = c.iz - a.iz;
        const bool textured = d.texture && d.tw > 0;
        const float e2x = -(e0x + e1x);
        const float r0 = e0x != 0 ? 1.0f / e0x : 0, r1 = e1x != 0 ? 1.0f / e1x : 0, r2 = e2x != 0 ? 1.0f / e2x : 0;
        for (int y = y0; y <= y1; ++y)
        {
            const float py = float(y) + 0.5f;
            // Where this row is inside all three edges: w0 >= 0, w1 >= 0, w0 + w1 <= 1.
            const float b0 = e0y * py + e0c, b1 = e1y * py + e1c;
            float lo = float(x0) + 0.5f, hi = float(x1) + 0.5f;
            auto limit = [&](float slope, float recip, float base) {
                // slope * x + base >= 0
                if (slope > 0)
                    lo = std::max(lo, -base * recip);
                else if (slope < 0)
                    hi = std::min(hi, -base * recip);
                else if (base < 0)
                    hi = lo - 1;
            };
            limit(e0x, r0, b0);
            limit(e1x, r1, b1);
            limit(e2x, r2, 1.0f - b0 - b1);
            if (hi < lo)
                continue;
            const int xs = std::max(x0, int(std::ceil(lo - 0.5f))), xe = std::min(x1, int(std::floor(hi - 0.5f)));
            float *depth = &depth_[std::size_t(y) * std::size_t(W_)];
            std::uint32_t *colour = &colour_[std::size_t(y) * std::size_t(W_)];
            for (int x = xs; x <= xe; ++x)
            {
                const float px = float(x) + 0.5f;
                const float wa = e0x * px + b0; // weight of a
                const float wb = e1x * px + b1; // weight of b
                const float wc = 1.0f - wa - wb;
                if (wa < -1e-4f || wb < -1e-4f || wc < -1e-4f)
                    continue;
                const float iz = a.iz + wb * dz1 + wc * dz2;
                if (iz <= depth[x])
                    continue;
                const float my = a.model_y + wb * dy1 + wc * dy2;
                float arm_px = -9;
                if (d.cutting)
                {
                    const float arm = a.arm + wb * da1 + wc * da2, leg = a.leg + wb * dg1 + wc * dg2;
                    if (arm > d.arm_max || leg < d.leg_min || leg > d.leg_max || my < d.y_low)
                        continue;
                    if (my > d.y_high && (!d.neckline || region == Neck || region == Head))
                        continue;
                    arm_px = arm;
                }
                const float tu = textured ? a.u + wb * du1 + wc * du2 : 0, tv = textured ? a.v + wb * dv1 + wc * dv2 : 0;
                if (d.alpha && textured)
                {
                    const int tx = int(tu * float(d.tw)) & (d.tw - 1), ty = int(tv * float(d.th)) & (d.th - 1);
                    const std::uint32_t al = d.texture[std::size_t(ty) * std::size_t(d.tw) + std::size_t(tx)] >> 24;
                    if (d.alpha == 4 && al < 128)
                        continue;
                    if (d.alpha == 8)
                    {
                        // Glass: blend over what is there, leaving the depth for what is behind.
                        if (!depth[x] || al == 0)
                            continue;
                        const std::uint32_t c = shade(d, tu, tv, a.light + wb * dl1 + wc * dl2, my, arm_px), o = colour[x];
                        auto mixc = [&](int sh) {
                            const int f = int((c >> sh) & 255), b = int((o >> sh) & 255);
                            return std::uint32_t(b + (f - b) * int(al) / 255) << sh;
                        };
                        colour[x] = mixc(16) | mixc(8) | mixc(0);
                        continue;
                    }
                }
                depth[x] = iz;
                colour[x] = shade(d, tu, tv, a.light + wb * dl1 + wc * dl2, my, arm_px);
            }
        }
        return 1;
    }

    const Kit &kit_;
    Look look_;
    const Body *body_ = nullptr;
    std::string bname_;
    std::uint32_t hide_ = 0;
    int hair_rule_ = 0;
    std::vector<Draw> draws_;
    std::map<std::string, Mip> tinted_;
    std::vector<M34> skin_;
    std::vector<std::uint8_t> use_, keep_;
    std::vector<Prepared> prepared_;
    std::vector<std::thread> pool_;
    std::mutex pool_lock_;
    std::condition_variable pool_go_, pool_done_;
    unsigned generation_ = 0;
    int band_rows_ = 0, band_done_ = 0;
    bool stop_ = false;
    std::vector<std::size_t> band_count_;
    std::vector<std::uint32_t> colour_;
    std::vector<float> depth_;
    int W_ = 0, H_ = 0;
    float tan_half_ = 0, dist_ = 1, center_y_ = 0, focal_ = 1, cy_ = 1, sy_ = 0;
    float body_scale_ = 1, head_scale_ = 1;
    bool baked_lighting_ = false;
    bool custom_rig_ = false;
    bool fit_standalone_ = false;
    float required_distance_ = 0.0f;
    const Texture *face_detail_ = nullptr;
    const Texture *front_trace_ = nullptr;
    std::size_t drawn_ = 0;
    int fields_body_ = -1;
    std::vector<float> arm_, leg_;
    float pelvis_y_ = 0, neck_y_ = 0;
    // Catalog kits: the hidden body triangles, the shaped head and its faces.
    std::vector<std::vector<std::uint8_t>> hidden_;
    Part head_;
    std::map<std::string, Mip> faces_;
};
} // namespace avkit

