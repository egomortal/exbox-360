// Ex-Box 360: the Music Library (songs on the console or a USB drive) and the
// background music player the 360's Guide offered.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Header-only, and it compiles the decoders (dr_mp3, dr_flac, dr_wav: public
// domain / MIT-0; stb_vorbis: public domain / MIT; see
// vendor/audio/NOTICE-EXBOX.md), so include it from exactly one
// translation unit per program (main.cpp, or a test). No SDL: the dashboard
// calls Player::mix from its audio callback.
//
// Threads: the main thread scans, decodes ahead (Player::pump) and changes
// what plays; the audio thread only calls Player::mix. Decoded audio passes
// between them through a single-producer, single-consumer ring. play(),
// next(), previous() and stop() empty the ring, so the caller must hold the
// audio device's lock (SDL_LockAudioDevice) around them; pump() and the
// queries need no lock.
#pragma once
#include "system_data.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <dirent.h>
#include <sys/stat.h>

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wall"
#pragma GCC diagnostic ignored "-Wextra"
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-value"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#pragma GCC diagnostic ignored "-Wshadow"
#pragma GCC diagnostic ignored "-Wtype-limits"
#pragma GCC diagnostic ignored "-Wclass-memaccess"
#endif
#define DR_MP3_IMPLEMENTATION
#define DR_FLAC_IMPLEMENTATION
#define DR_WAV_IMPLEMENTATION
#define DR_FLAC_NO_OGG
#define DR_FLAC_NO_WCHAR
#define DR_WAV_NO_WCHAR
#include "vendor/audio/dr_mp3.h"
#include "vendor/audio/dr_flac.h"
#include "vendor/audio/dr_wav.h"
#define STB_VORBIS_NO_PUSHDATA_API
#include "vendor/audio/stb_vorbis.c"
// stb_vorbis leaves its internal macros defined; they would clash with ordinary names.
#undef C
#undef CHECK
#undef CODEBOOK_ELEMENT
#undef CODEBOOK_ELEMENT_BASE
#undef CODEBOOK_ELEMENT_FAST
#undef CRC32_POLY
#undef DECODE
#undef DECODE_RAW
#undef DIVTAB_DENOM
#undef DIVTAB_NUMER
#undef EOP
#undef FALSE
#undef FAST_HUFFMAN_TABLE_MASK
#undef FAST_HUFFMAN_TABLE_SIZE
#undef INVALID_BITS
#undef L
#undef LIBVORBIS_MDCT
#undef LINE_OP
#undef MAX_BLOCKSIZE
#undef MAX_BLOCKSIZE_LOG
#undef NO_CODE
#undef PAGEFLAG_continued_packet
#undef PAGEFLAG_first_page
#undef PAGEFLAG_last_page
#undef PLAYBACK_LEFT
#undef PLAYBACK_MONO
#undef PLAYBACK_RIGHT
#undef R
#undef SAMPLE_unknown
#undef STBV_CDECL
#undef STBV_NOTUSED
#undef TRUE
#undef array_size_required
#undef temp_alloc
#undef temp_alloc_restore
#undef temp_alloc_save
#undef temp_block_array
#undef temp_free
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace music
{
constexpr int kRate = 48000;           // what the dashboard's audio device runs at
constexpr std::size_t kMaxSongs = 2000; // the most the library lists
constexpr int kMaxDepth = 6;           // folders inside a music folder

struct Song
{
    std::string path, title, artist, album, genre;
};

// ---------------------------------------------------------------- names, tags

inline std::string extension(const std::string &path)
{
    const std::size_t dot = path.rfind('.'), slash = path.rfind('/');
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash))
        return {};
    std::string ext = path.substr(dot + 1);
    for (char &c : ext)
        c = char(std::tolower(static_cast<unsigned char>(c)));
    return ext;
}

inline bool playable(const std::string &path)
{
    const std::string e = extension(path);
    return e == "mp3" || e == "flac" || e == "wav" || e == "ogg" || e == "oga";
}

inline std::string trim(std::string s)
{
    while (!s.empty() && (s.back() == ' ' || s.back() == '\0' || s.back() == '\r' || s.back() == '\n' || s.back() == '\t'))
        s.pop_back();
    std::size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t'))
        ++i;
    s.erase(0, i);
    if (s.size() > 200)
        s.resize(200);
    return s;
}

// "03 - Still Alive.mp3" -> "Still Alive"; "my_song.flac" -> "my song".
inline std::string title_from_file(const std::string &path)
{
    const std::size_t slash = path.rfind('/');
    std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
    const std::size_t dot = name.rfind('.');
    if (dot != std::string::npos && dot > 0)
        name.resize(dot);
    for (char &c : name)
        if (c == '_')
            c = ' ';
    // A leading track number ("03 ", "03 - ", "03. ", "1-03 "): one to three
    // digits, maybe a disc number before it, then a separator.
    auto digits = [&](std::size_t from) {
        std::size_t k = from;
        while (k < name.size() && k - from < 4 && std::isdigit(static_cast<unsigned char>(name[k])))
            ++k;
        return k - from >= 1 && k - from <= 3 ? k : from;
    };
    std::size_t i = digits(0);
    if (i > 0 && i + 1 < name.size() && (name[i] == '-' || name[i] == '.') && digits(i + 1) > i + 1)
        i = digits(i + 1);
    std::size_t j = i;
    while (j < name.size() && (name[j] == ' ' || name[j] == '.' || name[j] == '-'))
        ++j;
    if (i > 0 && j > i && j < name.size())
        name.erase(0, j);
    name = trim(name);
    return name.empty() ? "Untitled" : name;
}

inline void put_utf8(std::string &out, std::uint32_t cp)
{
    if (cp == 0)
        return;
    if (cp < 0x80)
        out += char(cp);
    else if (cp < 0x800)
    {
        out += char(0xC0 | (cp >> 6));
        out += char(0x80 | (cp & 0x3F));
    }
    else if (cp < 0x10000)
    {
        out += char(0xE0 | (cp >> 12));
        out += char(0x80 | ((cp >> 6) & 0x3F));
        out += char(0x80 | (cp & 0x3F));
    }
    else
    {
        out += char(0xF0 | (cp >> 18));
        out += char(0x80 | ((cp >> 12) & 0x3F));
        out += char(0x80 | ((cp >> 6) & 0x3F));
        out += char(0x80 | (cp & 0x3F));
    }
}

inline std::string latin1_to_utf8(const unsigned char *p, std::size_t n)
{
    std::string out;
    for (std::size_t i = 0; i < n && p[i]; ++i)
        put_utf8(out, p[i]);
    return out;
}

inline std::string utf16_to_utf8(const unsigned char *p, std::size_t n, bool big_endian)
{
    std::string out;
    for (std::size_t i = 0; i + 1 < n; i += 2)
    {
        std::uint32_t u = big_endian ? (p[i] << 8 | p[i + 1]) : (p[i + 1] << 8 | p[i]);
        if (u == 0)
            break;
        if (u >= 0xD800 && u < 0xDC00 && i + 3 < n)
        {
            const std::uint32_t lo = big_endian ? (p[i + 2] << 8 | p[i + 3]) : (p[i + 3] << 8 | p[i + 2]);
            if (lo >= 0xDC00 && lo < 0xE000)
            {
                u = 0x10000 + ((u - 0xD800) << 10) + (lo - 0xDC00);
                i += 2;
            }
        }
        else if (u >= 0xD800 && u < 0xE000)
            u = 0xFFFD;
        put_utf8(out, u);
    }
    return out;
}

// Keeps only well-formed UTF-8 (a tag may claim UTF-8 and hold Latin-1).
inline std::string valid_utf8(const std::string &s)
{
    std::string out;
    for (std::size_t i = 0; i < s.size();)
    {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        const int extra = c < 0x80 ? 0 : (c >> 5) == 6 ? 1 : (c >> 4) == 14 ? 2 : (c >> 3) == 30 ? 3 : -1;
        bool ok = extra >= 0 && i + std::size_t(extra) < s.size() + (extra == 0 ? 1 : 0);
        for (int k = 1; ok && k <= extra; ++k)
            ok = i + std::size_t(k) < s.size() && (static_cast<unsigned char>(s[i + std::size_t(k)]) >> 6) == 2;
        if (!ok)
        {
            put_utf8(out, c); // read it as Latin-1
            ++i;
            continue;
        }
        out.append(s, i, std::size_t(extra) + 1);
        i += std::size_t(extra) + 1;
    }
    return out;
}

// An ID3v2 text frame's body: encoding byte, then text (first value only).
inline std::string id3_text(const unsigned char *p, std::size_t n)
{
    if (n < 1)
        return {};
    const unsigned enc = p[0];
    ++p;
    --n;
    switch (enc)
    {
    case 0: return trim(latin1_to_utf8(p, n));
    case 1:
        if (n >= 2 && p[0] == 0xFE && p[1] == 0xFF)
            return trim(utf16_to_utf8(p + 2, n - 2, true));
        if (n >= 2 && p[0] == 0xFF && p[1] == 0xFE)
            return trim(utf16_to_utf8(p + 2, n - 2, false));
        return trim(utf16_to_utf8(p, n, false));
    case 2: return trim(utf16_to_utf8(p, n, true));
    case 3:
    {
        std::size_t len = 0;
        while (len < n && p[len])
            ++len;
        return trim(valid_utf8(std::string(reinterpret_cast<const char *>(p), len)));
    }
    default: return {};
    }
}

inline std::uint32_t syncsafe(const unsigned char *p)
{
    return std::uint32_t(p[0] & 0x7F) << 21 | std::uint32_t(p[1] & 0x7F) << 14 | std::uint32_t(p[2] & 0x7F) << 7 |
           std::uint32_t(p[3] & 0x7F);
}

// A genre as tags write it: ID3 often gives the ID3v1 number, "(17)", "17"
// or "(17)Rock", and "(RX)" / "(CR)" for remix and cover. Numbers beyond
// the original 80 genres are left as they are.
inline std::string genre_name(std::string g)
{
    static const char *const kGenres[] = {
        "Blues", "Classic Rock", "Country", "Dance", "Disco", "Funk", "Grunge", "Hip-Hop", "Jazz", "Metal",
        "New Age", "Oldies", "Other", "Pop", "R&B", "Rap", "Reggae", "Rock", "Techno", "Industrial",
        "Alternative", "Ska", "Death Metal", "Pranks", "Soundtrack", "Euro-Techno", "Ambient", "Trip-Hop", "Vocal", "Jazz+Funk",
        "Fusion", "Trance", "Classical", "Instrumental", "Acid", "House", "Game", "Sound Clip", "Gospel", "Noise",
        "AlternRock", "Bass", "Soul", "Punk", "Space", "Meditative", "Instrumental Pop", "Instrumental Rock", "Ethnic", "Gothic",
        "Darkwave", "Techno-Industrial", "Electronic", "Pop-Folk", "Eurodance", "Dream", "Southern Rock", "Comedy", "Cult", "Gangsta",
        "Top 40", "Christian Rap", "Pop/Funk", "Jungle", "Native American", "Cabaret", "New Wave", "Psychedelic", "Rave", "Showtunes",
        "Trailer", "Lo-Fi", "Tribal", "Acid Punk", "Acid Jazz", "Polka", "Retro", "Musical", "Rock & Roll", "Hard Rock"};
    g = trim(g);
    auto number = [](const std::string &t, int &out) {
        if (t.empty() || t.size() > 3)
            return false;
        out = 0;
        for (char c : t)
        {
            if (c < '0' || c > '9')
                return false;
            out = out * 10 + (c - '0');
        }
        return true;
    };
    int n = 0;
    if (g.size() > 2 && g[0] == '(')
    {
        const std::size_t close = g.find(')');
        if (close != std::string::npos)
        {
            const std::string inner = g.substr(1, close - 1), rest = trim(g.substr(close + 1));
            if (!rest.empty())
                return rest; // "(17)Rock": the words win
            if (inner == "RX")
                return "Remix";
            if (inner == "CR")
                return "Cover";
            if (number(inner, n))
                return n < 80 ? kGenres[n] : inner;
        }
    }
    if (number(g, n) && n < 80)
        return kGenres[n];
    return g;
}

// Title, artist, album and genre from a whole ID3v2 tag (versions 2.2 to 2.4).
inline void parse_id3v2(const std::vector<unsigned char> &tag, Song &song)
{
    if (tag.size() < 10 || std::memcmp(tag.data(), "ID3", 3) != 0)
        return;
    const int version = tag[3];
    const unsigned flags = tag[5];
    if (version < 2 || version > 4 || (flags & 0x80)) // unsynchronised tags are rare: skip them
        return;
    std::size_t end = std::min<std::size_t>(tag.size(), 10 + syncsafe(&tag[6]));
    std::size_t at = 10;
    if ((flags & 0x40) && version >= 3 && at + 4 <= end)
    {
        const std::uint32_t ext = version == 4 ? syncsafe(&tag[at])
                                               : (std::uint32_t(tag[at]) << 24 | tag[at + 1] << 16 | tag[at + 2] << 8 | tag[at + 3]) + 4;
        at += ext;
    }
    const std::size_t head = version == 2 ? 6 : 10;
    while (at + head <= end)
    {
        const unsigned char *f = &tag[at];
        if (f[0] == 0)
            break; // padding
        std::size_t size;
        std::string id;
        if (version == 2)
        {
            id.assign(reinterpret_cast<const char *>(f), 3);
            size = std::size_t(f[3]) << 16 | f[4] << 8 | f[5];
        }
        else
        {
            id.assign(reinterpret_cast<const char *>(f), 4);
            size = version == 4 ? syncsafe(f + 4) : (std::size_t(f[4]) << 24 | f[5] << 16 | f[6] << 8 | f[7]);
        }
        at += head;
        if (size > end - at)
            break;
        const bool compressed = version >= 3 && (f[9] & (version == 4 ? 0x0C : 0xC0));
        if (!compressed)
        {
            if (id == "TIT2" || id == "TT2")
                song.title = id3_text(&tag[at], size);
            else if (id == "TPE1" || id == "TP1")
                song.artist = id3_text(&tag[at], size);
            else if (id == "TALB" || id == "TAL")
                song.album = id3_text(&tag[at], size);
            else if (id == "TCON" || id == "TCO")
                song.genre = genre_name(id3_text(&tag[at], size));
        }
        at += size;
    }
}

inline bool iequal_prefix(const char *s, std::size_t n, const char *key)
{
    const std::size_t k = std::strlen(key);
    if (n <= k || s[k] != '=')
        return false;
    for (std::size_t i = 0; i < k; ++i)
        if (std::toupper(static_cast<unsigned char>(s[i])) != key[i])
            return false;
    return true;
}

// A Vorbis comment block (FLAC, and the layout inside Ogg's comment header).
inline void parse_vorbis_comments(const unsigned char *p, std::size_t n, Song &song)
{
    auto le32 = [&](std::size_t at) {
        return std::uint32_t(p[at]) | std::uint32_t(p[at + 1]) << 8 | std::uint32_t(p[at + 2]) << 16 | std::uint32_t(p[at + 3]) << 24;
    };
    if (n < 8)
        return;
    std::size_t at = 4 + std::size_t(le32(0));
    if (at + 4 > n)
        return;
    std::uint32_t count = le32(at);
    at += 4;
    for (std::uint32_t i = 0; i < count && at + 4 <= n; ++i)
    {
        const std::size_t len = le32(at);
        at += 4;
        if (len > n - at)
            break;
        const char *c = reinterpret_cast<const char *>(p + at);
        auto value = [&](const char *key) { return trim(valid_utf8(std::string(c + std::strlen(key) + 1, len - std::strlen(key) - 1))); };
        if (iequal_prefix(c, len, "TITLE") && song.title.empty())
            song.title = value("TITLE");
        else if (iequal_prefix(c, len, "ARTIST") && song.artist.empty())
            song.artist = value("ARTIST");
        else if (iequal_prefix(c, len, "ALBUM") && song.album.empty())
            song.album = value("ALBUM");
        else if (iequal_prefix(c, len, "GENRE") && song.genre.empty())
            song.genre = value("GENRE");
        at += len;
    }
}

// Reads an ID3v2 tag at the file's current position, if there is one, and
// leaves the file after it (or where it was).
inline std::vector<unsigned char> read_id3v2(FILE *f)
{
    const long start = std::ftell(f);
    unsigned char h[10];
    if (std::fread(h, 1, 10, f) != 10 || std::memcmp(h, "ID3", 3) != 0)
    {
        std::fseek(f, start, SEEK_SET);
        return {};
    }
    const std::uint32_t size = syncsafe(h + 6) + ((h[5] & 0x10) ? 10 : 0);
    std::vector<unsigned char> tag(h, h + 10);
    const std::size_t want = std::min<std::uint32_t>(size, 4u << 20); // pictures can be big: 4 MiB is plenty for text
    tag.resize(10 + want);
    tag.resize(10 + std::fread(tag.data() + 10, 1, want, f));
    std::fseek(f, start + long(10 + size), SEEK_SET);
    return tag;
}

// Steps over every ID3v2 tag in front (tools sometimes stack them), reading
// title, artist and album from them.
inline void skip_id3v2(FILE *f, Song *song = nullptr)
{
    for (int i = 0; i < 4; ++i)
    {
        const std::vector<unsigned char> tag = read_id3v2(f);
        if (tag.empty())
            return;
        if (song)
        {
            Song found;
            parse_id3v2(tag, found);
            if (song->title.empty())
                song->title = found.title;
            if (song->artist.empty())
                song->artist = found.artist;
            if (song->album.empty())
                song->album = found.album;
            if (song->genre.empty())
                song->genre = found.genre;
        }
    }
}

inline void read_flac_tags(FILE *f, Song &song)
{
    unsigned char magic[4];
    if (std::fread(magic, 1, 4, f) != 4 || std::memcmp(magic, "fLaC", 4) != 0)
        return;
    for (int blocks = 0; blocks < 64; ++blocks)
    {
        unsigned char h[4];
        if (std::fread(h, 1, 4, f) != 4)
            return;
        const std::size_t len = std::size_t(h[1]) << 16 | h[2] << 8 | h[3];
        if ((h[0] & 0x7F) == 4 && len < (1u << 20))
        {
            std::vector<unsigned char> block(len);
            if (std::fread(block.data(), 1, len, f) == len)
                parse_vorbis_comments(block.data(), len, song);
            return;
        }
        if (h[0] & 0x80)
            return; // the last metadata block
        if (std::fseek(f, long(len), SEEK_CUR) != 0)
            return;
    }
}

// Fills in title, artist, album and genre from the file's tags, or the file name.
inline void read_tags(Song &song)
{
    const std::string ext = extension(song.path);
    if (FILE *f = std::fopen(sysdata::local_path(song.path).c_str(), "rb"))
    {
        skip_id3v2(f, &song);
        if (ext == "flac")
            read_flac_tags(f, song);
        std::fclose(f);
    }
    if (ext == "ogg" || ext == "oga")
    {
        int error = 0;
        if (stb_vorbis *v = stb_vorbis_open_filename(sysdata::local_path(song.path).c_str(), &error, nullptr))
        {
            const stb_vorbis_comment c = stb_vorbis_get_comment(v);
            for (int i = 0; i < c.comment_list_length; ++i)
            {
                const char *s = c.comment_list[i];
                const std::size_t n = std::strlen(s);
                for (const char *key : {"TITLE", "ARTIST", "ALBUM", "GENRE"})
                    if (iequal_prefix(s, n, key))
                    {
                        std::string &field = key[1] == 'I' ? song.title : key[1] == 'R' ? song.artist : key[1] == 'L' ? song.album : song.genre;
                        if (field.empty())
                            field = trim(valid_utf8(std::string(s + std::strlen(key) + 1)));
                    }
            }
            stb_vorbis_close(v);
        }
    }
    if (song.title.empty())
        song.title = title_from_file(song.path);
}

// ---------------------------------------------------------------- cover art

// The picture inside an ID3v2 tag (APIC, or PIC in 2.2): the front cover when
// there is one, else the first picture. Returns the encoded image bytes.
inline std::vector<unsigned char> id3_picture(const std::vector<unsigned char> &tag)
{
    if (tag.size() < 10 || std::memcmp(tag.data(), "ID3", 3) != 0)
        return {};
    const int version = tag[3];
    if (version < 2 || version > 4 || (tag[5] & 0x80))
        return {};
    std::size_t end = std::min<std::size_t>(tag.size(), 10 + syncsafe(&tag[6]));
    std::size_t at = 10;
    if ((tag[5] & 0x40) && version >= 3 && at + 4 <= end)
        at += version == 4 ? syncsafe(&tag[at]) : (std::uint32_t(tag[at]) << 24 | tag[at + 1] << 16 | tag[at + 2] << 8 | tag[at + 3]) + 4;
    const std::size_t head = version == 2 ? 6 : 10;
    std::vector<unsigned char> first;
    while (at + head <= end)
    {
        const unsigned char *f = &tag[at];
        if (f[0] == 0)
            break;
        std::size_t size = version == 2 ? (std::size_t(f[3]) << 16 | f[4] << 8 | f[5])
                         : version == 4 ? syncsafe(f + 4) : (std::size_t(f[4]) << 24 | f[5] << 16 | f[6] << 8 | f[7]);
        const bool picture = version == 2 ? std::memcmp(f, "PIC", 3) == 0 : std::memcmp(f, "APIC", 4) == 0;
        at += head;
        if (size > end - at)
            break;
        if (picture && size > 4)
        {
            const unsigned char *d = &tag[at];
            const unsigned char encoding = d[0];
            std::size_t i = 1;
            if (version == 2)
                i += 3; // image format, three letters
            else
                while (i < size && d[i]) ++i;
            if (version != 2)
                ++i; // the MIME type's terminator
            if (i < size)
            {
                const int kind = d[i++];
                // the description: one or two zero bytes end it, by encoding
                if (encoding == 1 || encoding == 2)
                {
                    while (i + 1 < size && (d[i] || d[i + 1])) i += 2;
                    i += 2;
                }
                else
                {
                    while (i < size && d[i]) ++i;
                    ++i;
                }
                if (i < size)
                {
                    std::vector<unsigned char> image(d + i, d + size);
                    if (kind == 3)
                        return image;
                    if (first.empty())
                        first = std::move(image);
                }
            }
        }
        at += size;
    }
    return first;
}

// FLAC's PICTURE metadata block (also base64'd into Ogg comments).
inline std::vector<unsigned char> flac_picture_block(const unsigned char *p, std::size_t n, int *kind = nullptr)
{
    auto be32 = [&](std::size_t at) { return std::uint32_t(p[at]) << 24 | std::uint32_t(p[at + 1]) << 16 | std::uint32_t(p[at + 2]) << 8 | p[at + 3]; };
    if (n < 32)
        return {};
    std::size_t at = 0;
    const std::uint32_t type = be32(at); at += 4;
    const std::uint32_t mime = be32(at); at += 4;
    if (mime > n - at) return {};
    at += mime;
    if (at + 4 > n) return {};
    const std::uint32_t desc = be32(at); at += 4;
    if (desc > n - at) return {};
    at += desc + 16; // width, height, depth, colours
    if (at + 4 > n) return {};
    const std::uint32_t len = be32(at); at += 4;
    if (len > n - at) return {};
    if (kind) *kind = int(type);
    return std::vector<unsigned char>(p + at, p + at + len);
}

inline std::vector<unsigned char> base64(const std::string &text)
{
    std::vector<unsigned char> out;
    unsigned value = 0;
    int bits = 0;
    for (char c : text)
    {
        int v = c >= 'A' && c <= 'Z' ? c - 'A' : c >= 'a' && c <= 'z' ? c - 'a' + 26 : c >= '0' && c <= '9' ? c - '0' + 52
              : c == '+' ? 62 : c == '/' ? 63 : -1;
        if (v < 0)
            continue;
        value = value << 6 | unsigned(v);
        bits += 6;
        if (bits >= 8)
        {
            bits -= 8;
            out.push_back(static_cast<unsigned char>(value >> bits));
        }
    }
    return out;
}

inline std::vector<unsigned char> read_file(const std::string &path, std::size_t limit = 8u << 20)
{
    std::vector<unsigned char> data;
    if (FILE *f = std::fopen(sysdata::local_path(path).c_str(), "rb"))
    {
        std::fseek(f, 0, SEEK_END);
        const long size = std::ftell(f);
        std::fseek(f, 0, SEEK_SET);
        if (size > 0 && std::size_t(size) <= limit)
        {
            data.resize(std::size_t(size));
            data.resize(std::fread(data.data(), 1, data.size(), f));
        }
        std::fclose(f);
    }
    return data;
}

// The covers the owner chose (the Music Library's Choose Cover): a song's
// path to a picture's. They come before anything else.
inline std::map<std::string, std::string> &chosen_covers()
{
    static std::map<std::string, std::string> chosen;
    return chosen;
}
// The chosen covers file: one "song<TAB>picture" a line.
inline void read_chosen_covers(const std::string &file)
{
    chosen_covers().clear();
    if (FILE *f = std::fopen(file.c_str(), "rb"))
    {
        char line[4096];
        while (std::fgets(line, sizeof line, f))
        {
            std::string l = line;
            while (!l.empty() && (l.back() == '\n' || l.back() == '\r'))
                l.pop_back();
            const std::size_t tab = l.find('\t');
            if (tab != std::string::npos && tab > 0 && tab + 1 < l.size())
                chosen_covers()[l.substr(0, tab)] = l.substr(tab + 1);
        }
        std::fclose(f);
    }
}
inline bool write_chosen_covers(const std::string &file)
{
    FILE *f = std::fopen(file.c_str(), "wb");
    if (!f)
        return false;
    for (const auto &entry : chosen_covers())
        std::fprintf(f, "%s\t%s\n", entry.first.c_str(), entry.second.c_str());
    return std::fclose(f) == 0;
}

// A song's cover, in this order: the picture the owner chose for it, its own
// picture beside it (<song>.jpg/.png) or in /data/nxe/music-art/, the picture
// embedded in the file, then the folder's cover (cover / folder / front /
// AlbumArt / album .jpg/.png).
inline std::vector<unsigned char> cover_art(const std::string &path, const std::string &art_root = "/data/nxe/music-art")
{
    if (const auto chosen = chosen_covers().find(path); chosen != chosen_covers().end())
    {
        std::vector<unsigned char> data = read_file(chosen->second, 32u << 20);
        if (!data.empty())
            return data;
    }
    const std::size_t slash = path.rfind('/');
    const std::string dir = slash == std::string::npos ? "." : path.substr(0, slash);
    std::string stem = slash == std::string::npos ? path : path.substr(slash + 1);
    if (const std::size_t dot = stem.rfind('.'); dot != std::string::npos)
        stem.resize(dot);
    for (const std::string &base : {dir + "/" + stem, art_root + "/" + stem})
        for (const char *ext : {".jpg", ".jpeg", ".png", ".JPG", ".PNG"})
        {
            std::vector<unsigned char> data = read_file(base + ext);
            if (!data.empty())
                return data;
        }
    const std::string ext = extension(path);
    if (FILE *f = std::fopen(sysdata::local_path(path).c_str(), "rb"))
    {
        std::vector<unsigned char> found;
        for (int i = 0; i < 4 && found.empty(); ++i)
        {
            const std::vector<unsigned char> tag = read_id3v2(f);
            if (tag.empty())
                break;
            found = id3_picture(tag);
        }
        if (found.empty() && ext == "flac")
        {
            unsigned char magic[4];
            if (std::fread(magic, 1, 4, f) == 4 && std::memcmp(magic, "fLaC", 4) == 0)
                for (int blocks = 0; blocks < 64; ++blocks)
                {
                    unsigned char h[4];
                    if (std::fread(h, 1, 4, f) != 4)
                        break;
                    const std::size_t len = std::size_t(h[1]) << 16 | h[2] << 8 | h[3];
                    if ((h[0] & 0x7F) == 6 && len < (16u << 20))
                    {
                        std::vector<unsigned char> block(len);
                        if (std::fread(block.data(), 1, len, f) != len)
                            break;
                        int kind = 0;
                        std::vector<unsigned char> image = flac_picture_block(block.data(), len, &kind);
                        if (!image.empty() && (kind == 3 || found.empty()))
                            found = std::move(image);
                        if (kind == 3)
                            break;
                    }
                    else if (std::fseek(f, long(len), SEEK_CUR) != 0)
                        break;
                    if (h[0] & 0x80)
                        break;
                }
        }
        std::fclose(f);
        if (!found.empty())
            return found;
    }
    if (ext == "ogg" || ext == "oga")
    {
        int error = 0;
        if (stb_vorbis *v = stb_vorbis_open_filename(sysdata::local_path(path).c_str(), &error, nullptr))
        {
            std::vector<unsigned char> found;
            const stb_vorbis_comment c = stb_vorbis_get_comment(v);
            for (int i = 0; i < c.comment_list_length && found.empty(); ++i)
            {
                const char *s = c.comment_list[i];
                const std::size_t n = std::strlen(s);
                if (iequal_prefix(s, n, "METADATA_BLOCK_PICTURE"))
                {
                    const std::vector<unsigned char> block = base64(s + 23);
                    found = flac_picture_block(block.data(), block.size());
                }
            }
            stb_vorbis_close(v);
            if (!found.empty())
                return found;
        }
    }
    for (const char *name : {"cover", "Cover", "folder", "Folder", "front", "Front", "AlbumArt", "albumart", "album", "Album"})
        for (const char *e : {".jpg", ".jpeg", ".png"})
        {
            std::vector<unsigned char> data = read_file(dir + "/" + name + e);
            if (!data.empty())
                return data;
        }
    return {};
}

// ---------------------------------------------------------------- the library

inline bool is_directory(const std::string &path)
{
    struct stat st;
    return sysdata::stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

inline void walk(const std::string &dir, int depth, std::vector<std::string> &out)
{
    if (depth > kMaxDepth || out.size() >= kMaxSongs)
        return;
    std::vector<std::string> names;
    for (const auto &entry:sysdata::list(dir))
        if (!entry.name.empty() && entry.name[0]!='.') names.push_back(entry.name);
    std::sort(names.begin(), names.end());
    for (const auto &name : names)
    {
        if (out.size() >= kMaxSongs)
            return;
        const std::string path = dir + "/" + name;
        if (playable(path))
            out.push_back(path);
        else if (extension(path).empty() && is_directory(path))
            walk(path, depth + 1, out);
    }
}

inline std::string folded(const std::string &s)
{
    std::string out = s;
    for (char &c : out)
        c = char(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

// Every playable file under the roots, in folder order (albums stay together).
// Tags are read once per file and kept in `known` for the next look.
inline std::vector<Song> scan(const std::vector<std::string> &roots, std::map<std::string, Song> &known)
{
    std::vector<std::string> paths;
    for (const auto &root : roots)
        if (is_directory(root))
            walk(root, 0, paths);
    std::sort(paths.begin(), paths.end(), [](const std::string &a, const std::string &b) { return folded(a) < folded(b); });
    paths.erase(std::unique(paths.begin(), paths.end()), paths.end());
    std::vector<Song> songs;
    for (const auto &path : paths)
    {
        auto it = known.find(path);
        if (it == known.end())
        {
            Song s;
            s.path = path;
            read_tags(s);
            it = known.emplace(path, s).first;
        }
        songs.push_back(it->second);
    }
    return songs;
}

// ---------------------------------------------------------------- decoding

// One song, decoded to interleaved 16-bit samples at its own rate and channels.
class Decoder
{
  public:
    Decoder() = default;
    Decoder(const Decoder &) = delete;
    Decoder &operator=(const Decoder &) = delete;
    ~Decoder()
    {
        close();
    }

    bool open(const std::string &path)
    {
        close();
        const std::string ext = extension(path);
        if (ext == "mp3")
        {
            mp3_ = std::make_unique<drmp3>();
            if (!drmp3_init_file(mp3_.get(), sysdata::local_path(path).c_str(), nullptr))
            {
                mp3_.reset();
                return false;
            }
            kind_ = Mp3;
            channels_ = mp3_->channels;
            rate_ = mp3_->sampleRate;
        }
        else if (ext == "flac")
        {
            // Through our own reader, so a tag in front (an ID3v2 tag, which
            // some taggers add and dr_flac does not skip) is stepped over.
            flac_file_ = std::fopen(sysdata::local_path(path).c_str(), "rb");
            if (!flac_file_)
                return false;
            skip_id3v2(flac_file_);
            flac_base_ = std::ftell(flac_file_);
            flac_ = drflac_open(flac_read, flac_seek, flac_tell, this, nullptr);
            if (!flac_)
            {
                close();
                return false;
            }
            kind_ = Flac;
            channels_ = flac_->channels;
            rate_ = flac_->sampleRate;
            length_ = flac_->totalPCMFrameCount;
        }
        else if (ext == "wav")
        {
            wav_ = std::make_unique<drwav>();
            if (!drwav_init_file(wav_.get(), sysdata::local_path(path).c_str(), nullptr))
            {
                wav_.reset();
                return false;
            }
            kind_ = Wav;
            channels_ = wav_->channels;
            rate_ = wav_->sampleRate;
            length_ = wav_->totalPCMFrameCount;
        }
        else if (ext == "ogg" || ext == "oga")
        {
            int error = 0;
            ogg_ = stb_vorbis_open_filename(sysdata::local_path(path).c_str(), &error, nullptr);
            if (!ogg_)
                return false;
            kind_ = Ogg;
            const stb_vorbis_info info = stb_vorbis_get_info(ogg_);
            channels_ = unsigned(std::max(0, info.channels));
            rate_ = info.sample_rate;
            length_ = stb_vorbis_stream_length_in_samples(ogg_);
        }
        else
            return false;
        if (channels_ < 1 || channels_ > 8 || rate_ < 8000 || rate_ > 384000)
        {
            close();
            return false;
        }
        return true;
    }

    void close()
    {
        if (mp3_)
            drmp3_uninit(mp3_.get());
        mp3_.reset();
        if (flac_)
            drflac_close(flac_);
        flac_ = nullptr;
        if (flac_file_)
            std::fclose(flac_file_);
        flac_file_ = nullptr;
        if (wav_)
            drwav_uninit(wav_.get());
        wav_.reset();
        if (ogg_)
            stb_vorbis_close(ogg_);
        ogg_ = nullptr;
        kind_ = None;
        channels_ = rate_ = 0;
        length_ = 0;
    }

    bool is_open() const
    {
        return kind_ != None;
    }
    unsigned channels() const
    {
        return channels_;
    }
    unsigned rate() const
    {
        return rate_;
    }
    // Frames in the song, or 0 when the format does not say (MP3).
    std::uint64_t length() const
    {
        return length_;
    }

    // Up to `frames` frames into out (frames * channels() samples); 0 at the end.
    std::size_t read(std::int16_t *out, std::size_t frames)
    {
        switch (kind_)
        {
        case Mp3: return std::size_t(drmp3_read_pcm_frames_s16(mp3_.get(), frames, out));
        case Flac: return std::size_t(drflac_read_pcm_frames_s16(flac_, frames, out));
        case Wav: return std::size_t(drwav_read_pcm_frames_s16(wav_.get(), frames, out));
        case Ogg:
            return std::size_t(std::max(0, stb_vorbis_get_samples_short_interleaved(ogg_, int(channels_), out, int(frames * channels_))));
        case None: break;
        }
        return 0;
    }

  private:
    static size_t flac_read(void *self, void *out, size_t bytes)
    {
        return std::fread(out, 1, bytes, static_cast<Decoder *>(self)->flac_file_);
    }
    static drflac_bool32 flac_seek(void *self, int offset, drflac_seek_origin origin)
    {
        Decoder *d = static_cast<Decoder *>(self);
        const int ok = origin == DRFLAC_SEEK_SET   ? std::fseek(d->flac_file_, d->flac_base_ + offset, SEEK_SET)
                       : origin == DRFLAC_SEEK_CUR ? std::fseek(d->flac_file_, offset, SEEK_CUR)
                                                   : std::fseek(d->flac_file_, offset, SEEK_END);
        return ok == 0;
    }
    static drflac_bool32 flac_tell(void *self, drflac_int64 *cursor)
    {
        Decoder *d = static_cast<Decoder *>(self);
        *cursor = std::ftell(d->flac_file_) - d->flac_base_;
        return *cursor >= 0;
    }

    enum Kind
    {
        None,
        Mp3,
        Flac,
        Wav,
        Ogg
    } kind_ = None;
    std::unique_ptr<drmp3> mp3_;
    drflac *flac_ = nullptr;
    FILE *flac_file_ = nullptr;
    long flac_base_ = 0; // where the FLAC stream starts in the file
    std::unique_ptr<drwav> wav_;
    stb_vorbis *ogg_ = nullptr;
    unsigned channels_ = 0, rate_ = 0;
    std::uint64_t length_ = 0;
};

// Any rate to 48 kHz, by linear interpolation; stereo in, stereo out.
class Resampler
{
  public:
    void reset(unsigned rate)
    {
        step_ = double(rate) / double(kRate);
        pos_ = 0;
        have_prev_ = false;
    }
    // Takes `frames` stereo frames (in, interleaved) and appends 48 kHz stereo to out.
    void push(const std::int16_t *in, std::size_t frames, std::vector<std::int16_t> &out)
    {
        if (frames == 0)
            return;
        if (step_ == 1.0)
        {
            out.insert(out.end(), in, in + frames * 2);
            return;
        }
        // Positions count source frames from in[0]; frame -1 is the last
        // frame of the previous call, kept in prev_.
        auto at = [&](long i, int c) -> double { return i < 0 ? prev_[c] : in[std::size_t(i) * 2 + std::size_t(c)]; };
        double p = have_prev_ ? pos_ - 1 : 0;
        const double last = double(frames - 1);
        while (p < last)
        {
            const long i = long(std::floor(p));
            const double t = p - double(i);
            for (int c = 0; c < 2; ++c)
            {
                const double a = at(i, c), b = at(i + 1, c);
                out.push_back(std::int16_t(std::clamp(a + (b - a) * t, -32768.0, 32767.0)));
            }
            p += step_;
        }
        prev_[0] = in[(frames - 1) * 2];
        prev_[1] = in[(frames - 1) * 2 + 1];
        pos_ = p - last; // measured from the kept frame
        have_prev_ = true;
    }

  private:
    double step_ = 1, pos_ = 0;
    std::int16_t prev_[2] = {0, 0};
    bool have_prev_ = false;
};

// Decoded 48 kHz stereo between the main thread and the audio thread.
class Ring
{
  public:
    static constexpr std::size_t kFrames = 1u << 17; // about 2.7 seconds

    std::size_t space() const
    {
        return kFrames - std::size_t(write_.load(std::memory_order_relaxed) - read_.load(std::memory_order_acquire));
    }
    std::size_t queued() const
    {
        return std::size_t(write_.load(std::memory_order_acquire) - read_.load(std::memory_order_acquire));
    }
    // Producer: appends as many frames as fit; returns how many.
    std::size_t write(const std::int16_t *in, std::size_t frames)
    {
        const std::uint64_t w = write_.load(std::memory_order_relaxed);
        const std::size_t n = std::min(frames, space());
        for (std::size_t i = 0; i < n; ++i)
        {
            const std::size_t at = std::size_t((w + i) & (kFrames - 1)) * 2;
            data_[at] = in[i * 2];
            data_[at + 1] = in[i * 2 + 1];
        }
        write_.store(w + n, std::memory_order_release);
        return n;
    }
    // Consumer: adds up to `frames` frames, scaled by volume/256, into acc
    // (interleaved stereo); returns how many it had.
    std::size_t mix_into(int *acc, std::size_t frames, int volume)
    {
        const std::uint64_t r = read_.load(std::memory_order_relaxed);
        const std::size_t n = std::min<std::size_t>(frames, std::size_t(write_.load(std::memory_order_acquire) - r));
        for (std::size_t i = 0; i < n; ++i)
        {
            const std::size_t at = std::size_t((r + i) & (kFrames - 1)) * 2;
            acc[i * 2] += data_[at] * volume / 256;
            acc[i * 2 + 1] += data_[at + 1] * volume / 256;
        }
        read_.store(r + n, std::memory_order_release);
        return n;
    }
    // Total frames written and read since the start (they never go back).
    std::uint64_t written() const
    {
        return write_.load(std::memory_order_acquire);
    }
    std::uint64_t consumed() const
    {
        return read_.load(std::memory_order_acquire);
    }
    // Drops everything queued. Only with the consumer stopped (the audio lock held).
    void clear()
    {
        read_.store(write_.load(std::memory_order_relaxed), std::memory_order_release);
    }
    // Takes back what was written from `written` on (not what is heard). Only
    // with the consumer stopped (the audio lock held).
    void truncate(std::uint64_t written)
    {
        const std::uint64_t read = read_.load(std::memory_order_acquire);
        if (written < write_.load(std::memory_order_relaxed))
            write_.store(std::max(written, read), std::memory_order_release);
    }

  private:
    std::vector<std::int16_t> data_ = std::vector<std::int16_t>(kFrames * 2);
    std::atomic<std::uint64_t> write_{0}, read_{0};
};

// ---------------------------------------------------------------- the player

// What happens at the end of the list (All: from the top again) or of each
// song (One: that song again), as the 360's player offered.
enum class Repeat
{
    Off,
    All,
    One
};

// Plays a list of songs in order (or shuffled) in the background, as the 360
// played music from the Guide while you used the dashboard.
class Player
{
  public:
    // Caller holds the audio lock. Starts list[index]; songs that will not open
    // are skipped. Shuffled, list[index] plays first and the rest follow in a
    // random order.
    void play(std::vector<Song> list, int index)
    {
        list_ = std::move(list);
        natural_.resize(list_.size());
        for (std::size_t i = 0; i < natural_.size(); ++i)
            natural_[i] = int(i);
        ring_.clear();
        marks_.clear();
        pending_.clear(); // the last song's resampled tail must not follow
        pending_at_ = 0;
        ended_ = false;
        idle_opens_ = 0;
        paused_.store(false);
        if (shuffle_ && index >= 0 && index < int(list_.size()))
        {
            std::vector<int> order{index};
            for (int i = 0; i < int(list_.size()); ++i)
                if (i != index)
                    order.push_back(i);
            shuffle_tail(order, 1);
            reorder(order);
            index = 0;
        }
        open_from(index, +1);
    }
    // Caller holds the audio lock. On, the songs still to come are put in a
    // random order after the one playing; off, the list goes back to its own
    // order and carries on after the song playing. The song heard plays on;
    // one already decoded ahead after it is taken back, so the new order
    // starts with the very next song.
    void set_shuffle(bool on, std::uint32_t seed = 1)
    {
        seed_ = seed ? seed : 1;
        if (on == shuffle_)
            return;
        shuffle_ = on;
        const int at = current(); // also forgets marks already heard
        if (at < 0 || list_.empty())
            return;
        const bool ahead = marks_.size() > 1 || decoding_ != at;
        if (ahead)
        {
            if (marks_.size() > 1)
                ring_.truncate(marks_[1].start);
            marks_.resize(1);
            pending_.clear();
            pending_at_ = 0;
            decoder_.close();
            decoding_ = -1;
            ended_ = false;
        }
        std::vector<int> order;
        if (on)
        {
            order.push_back(at);
            for (int i = 0; i < int(list_.size()); ++i)
                if (i != at)
                    order.push_back(i);
            shuffle_tail(order, 1);
        }
        else
        {
            order.resize(list_.size());
            for (std::size_t i = 0; i < order.size(); ++i)
                order[std::size_t(natural_[i])] = int(i);
        }
        reorder(order);
        if (ahead)
            open_after(marks_.front().index);
    }
    bool shuffle() const
    {
        return shuffle_;
    }
    // Main thread.
    void set_repeat(Repeat repeat)
    {
        repeat_ = repeat;
    }
    Repeat repeat() const
    {
        return repeat_;
    }
    // Caller holds the audio lock.
    void stop()
    {
        decoder_.close();
        ring_.clear();
        marks_.clear();
        list_.clear();
        natural_.clear();
        decoding_ = -1;
        ended_ = false;
        paused_.store(false);
    }
    // Caller holds the audio lock. The next song now (stops after the last,
    // unless repeating: then the first again).
    void next()
    {
        const int at = current();
        if (at < 0)
            return;
        if (at + 1 >= int(list_.size()))
        {
            if (repeat_ == Repeat::Off)
                stop();
            else
                restart(0, +1);
            return;
        }
        restart(at + 1, +1);
    }
    // Caller holds the audio lock. Back to the start of this song, or (in its
    // first three seconds) the one before (repeating the list: the last from
    // the first).
    void previous()
    {
        const int at = current();
        if (at < 0)
            return;
        const bool back = elapsed_seconds() < 3;
        if (back && at == 0 && repeat_ == Repeat::All && list_.size() > 1)
            restart(int(list_.size()) - 1, -1);
        else
            restart(back && at > 0 ? at - 1 : at, -1);
    }
    void set_paused(bool paused)
    {
        paused_.store(paused);
    }
    bool paused() const
    {
        return paused_.load();
    }
    bool active()
    {
        return current() >= 0;
    }

    // Main thread, every frame: decodes ahead until the ring is full (or for
    // at most `budget` output frames) and moves on to the next song at the end
    // of one, without a gap.
    void pump(std::size_t budget = kRate / 2)
    {
        std::size_t made = 0;
        while (decoding_ >= 0 && made < budget)
        {
            if (!pending_.empty())
            {
                const std::size_t frames = pending_.size() / 2 - pending_at_;
                const std::size_t n = ring_.write(pending_.data() + pending_at_ * 2, frames);
                pending_at_ += n;
                made += n;
                if (pending_at_ * 2 < pending_.size())
                    return; // the ring is full
                pending_.clear();
                pending_at_ = 0;
                continue;
            }
            const unsigned ch = decoder_.channels();
            const std::size_t got = decoder_.read(raw_, 1024);
            if (got == 0)
            {
                // The end of this song: the next one follows straight on (or
                // this one again, or the first after the last, when repeating).
                // Songs that open but hold nothing stop a repeat going round
                // and round without a sound.
                if (decoded_ == 0)
                    ++idle_opens_;
                open_after(decoding_);
                continue;
            }
            decoded_ += got;
            idle_opens_ = 0;
            for (std::size_t i = 0; i < got; ++i)
            {
                stereo_[i * 2] = raw_[i * ch];
                stereo_[i * 2 + 1] = raw_[i * ch + (ch > 1 ? 1 : 0)];
            }
            resampler_.push(stereo_, got, pending_);
            pending_at_ = 0;
        }
    }

    // Audio thread: adds the music into acc (interleaved stereo, `frames` frames).
    void mix(int *acc, std::size_t frames, int volume = 205)
    {
        if (paused_.load())
            return;
        ring_.mix_into(acc, frames, volume);
    }

    // Main thread: the song being heard now (index into the list), or -1.
    int current()
    {
        const std::uint64_t heard = ring_.consumed();
        while (marks_.size() > 1 && marks_[1].start <= heard)
            marks_.pop_front();
        if (marks_.empty())
            return -1;
        if (ended_ && decoding_ < 0 && heard >= ring_.written() && pending_.empty())
        {
            marks_.clear(); // the list has played to the end
            list_.clear();
            ended_ = false;
            return -1;
        }
        return marks_.front().index;
    }
    const Song *current_song()
    {
        const int i = current();
        return i >= 0 && i < int(list_.size()) ? &list_[std::size_t(i)] : nullptr;
    }
    double elapsed_seconds()
    {
        if (current() < 0)
            return 0;
        return double(ring_.consumed() - marks_.front().start) / kRate;
    }
    // The song's length, or 0 when its format does not say.
    double length_seconds()
    {
        if (current() < 0)
            return 0;
        return marks_.front().seconds;
    }
    const std::vector<Song> &list() const
    {
        return list_;
    }

    // ---- The current playlist, edited while it plays (the 360's "Add to
    // Current Playlist" and Edit Current Playlist). Each takes back anything
    // decoded ahead after the song heard, so the very next song follows the
    // new list; the song heard plays on, gaplessly.

    // Caller holds the audio lock. Songs added at the end; with nothing
    // playing, they start playing from the first.
    void append(const std::vector<Song> &songs)
    {
        if (songs.empty())
            return;
        const int at = current();
        if (at < 0)
        {
            play(songs, 0);
            return;
        }
        drop_ahead();
        for (const Song &song : songs)
        {
            natural_.push_back(int(list_.size()));
            list_.push_back(song);
        }
        resume_after(at);
    }
    // Caller holds the audio lock. Takes list[index] out; the song heard is
    // skipped to the one after it (or the music stops when it was the last).
    void remove(int index)
    {
        const int at = current();
        if (at < 0 || index < 0 || index >= int(list_.size()))
            return;
        if (index == at)
        {
            if (list_.size() == 1)
            {
                stop();
                return;
            }
            const int removed = natural_[std::size_t(index)];
            list_.erase(list_.begin() + index);
            natural_.erase(natural_.begin() + index);
            for (int &n : natural_)
                if (n > removed)
                    --n;
            const bool was_last = index >= int(list_.size());
            if (was_last && repeat_ == Repeat::Off)
            {
                stop();
                return;
            }
            const bool paused = paused_.load();
            restart(was_last ? 0 : index, +1);
            paused_.store(paused);
            return;
        }
        drop_ahead();
        std::vector<int> order;
        for (int i = 0; i < int(list_.size()); ++i)
            if (i != index)
                order.push_back(i);
        const int removed = natural_[std::size_t(index)];
        reorder(order);
        for (int &n : natural_)
            if (n > removed)
                --n;
        resume_after(marks_.front().index);
    }
    // Caller holds the audio lock. list[index] moves to place `to`.
    void move(int index, int to)
    {
        const int n = int(list_.size());
        if (current() < 0 || index < 0 || index >= n || to < 0 || to >= n || index == to)
            return;
        drop_ahead();
        std::vector<int> order;
        for (int i = 0; i < n; ++i)
            if (i != index)
                order.push_back(i);
        order.insert(order.begin() + to, index);
        reorder(order);
        if (!shuffle_) // the edited order is the list's own order now
            for (int i = 0; i < n; ++i)
                natural_[std::size_t(i)] = i;
        resume_after(marks_.front().index);
    }
    // Caller holds the audio lock. Plays list[index] now, the rest following.
    void jump(int index)
    {
        if (index < 0 || index >= int(list_.size()))
            return;
        const bool paused = paused_.load();
        restart(index, +1);
        paused_.store(paused);
    }

  private:
    // Forgets what was decoded after the song heard (that song plays on).
    void drop_ahead()
    {
        const int at = current();
        if (at < 0)
            return;
        if (marks_.size() > 1)
            ring_.truncate(marks_[1].start);
        marks_.resize(1);
        if (decoding_ != at)
        {
            pending_.clear();
            pending_at_ = 0;
            decoder_.close();
            decoding_ = -1;
        }
        ended_ = false;
    }
    // After drop_ahead: when nothing is decoding (the song heard was fully
    // decoded), open what follows it in the new list.
    void resume_after(int at)
    {
        if (decoding_ < 0)
            open_after(at);
    }
    struct Mark
    {
        std::uint64_t start; // in ring frames written
        int index;
        double seconds;
    };

    void restart(int index, int direction)
    {
        decoder_.close();
        ring_.clear();
        marks_.clear();
        pending_.clear();
        pending_at_ = 0;
        ended_ = false;
        idle_opens_ = 0;
        paused_.store(false);
        open_from(index, direction);
    }
    // What follows list_[index] when it ends: the next song, or as repeating says.
    void open_after(int index)
    {
        const bool looping = repeat_ != Repeat::Off && idle_opens_ <= int(list_.size());
        if (looping && repeat_ == Repeat::One)
            open_from(index, +1);
        else if (index + 1 < int(list_.size()))
            open_from(index + 1, +1);
        else if (looping)
            open_from(0, +1);
        else
        {
            decoder_.close();
            decoding_ = -1;
            ended_ = true;
        }
    }
    // Fisher-Yates over order[from..], from the seed (xorshift32).
    void shuffle_tail(std::vector<int> &order, std::size_t from)
    {
        for (std::size_t i = order.size(); i > from + 1; --i)
        {
            seed_ ^= seed_ << 13;
            seed_ ^= seed_ >> 17;
            seed_ ^= seed_ << 5;
            const std::size_t j = from + seed_ % (i - from);
            std::swap(order[i - 1], order[j]);
        }
    }
    // The list becomes list_[order[0]], list_[order[1]] ...; the song heard and
    // the one decoding keep playing under their new places.
    void reorder(const std::vector<int> &order)
    {
        std::vector<Song> list;
        std::vector<int> natural, place(list_.size(), -1);
        list.reserve(order.size());
        for (int i : order)
        {
            place[std::size_t(i)] = int(list.size());
            list.push_back(std::move(list_[std::size_t(i)]));
            natural.push_back(natural_[std::size_t(i)]);
        }
        for (Mark &m : marks_)
            m.index = place[std::size_t(m.index)];
        if (decoding_ >= 0)
            decoding_ = place[std::size_t(decoding_)];
        list_ = std::move(list);
        natural_ = std::move(natural);
    }
    // Opens list[index], or the next one that opens going `direction`.
    void open_from(int index, int direction)
    {
        decoder_.close();
        decoding_ = -1;
        for (int i = index; i >= 0 && i < int(list_.size()); i += direction)
            if (decoder_.open(list_[std::size_t(i)].path))
            {
                decoding_ = i;
                decoded_ = 0;
                resampler_.reset(decoder_.rate());
                const std::uint64_t start = ring_.written() + pending_.size() / 2 - pending_at_;
                marks_.push_back({start, i, decoder_.length() ? double(decoder_.length()) / decoder_.rate() : 0});
                return;
            }
        ended_ = !marks_.empty();
    }

    std::vector<Song> list_;
    std::vector<int> natural_; // each song's place in the list as play() was given it
    Decoder decoder_;
    Resampler resampler_;
    Ring ring_;
    std::vector<std::int16_t> pending_; // resampled, not yet in the ring
    std::size_t pending_at_ = 0;        // frames of pending_ already written
    std::deque<Mark> marks_;
    std::int16_t raw_[1024 * 8];    // one read: up to 1024 frames of up to 8 channels
    std::int16_t stereo_[1024 * 2]; // the same as stereo
    int decoding_ = -1;
    std::size_t decoded_ = 0; // frames read from the song decoding
    int idle_opens_ = 0;      // songs in a row that ended with nothing in them
    bool ended_ = false;
    bool shuffle_ = false;
    Repeat repeat_ = Repeat::Off;
    std::uint32_t seed_ = 1;
    std::atomic<bool> paused_{false};
};

// Where the library looks: the console's own folder, then USB drives.
inline std::vector<std::string> default_roots()
{
    return {"/data/nxe/music", "/mnt/usb0/Music", "/mnt/usb1/Music", "/mnt/usb0/music", "/mnt/usb1/music",
            "/mnt/ext0/Music", "/mnt/ext1/Music"};
}

// Opt-in roots for already-accessible, owner-supplied game files. This does
// not mount images, decrypt containers or download anything. A setup may add
// one absolute directory per line to /data/nxe/game-music-roots.txt; common
// removable-drive layouts work without a sidecar.
inline std::vector<std::string> game_roots()
{
    std::vector<std::string> roots={"/data/roms", "/data/games", "/mnt/usb0/roms", "/mnt/usb1/roms",
                                    "/mnt/usb0/ROMs", "/mnt/usb1/ROMs", "/mnt/usb0/Games", "/mnt/usb1/Games"};
    if (FILE *f=std::fopen(sysdata::local_path("/data/nxe/game-music-roots.txt").c_str(),"rb")) {
        char line[4096];
        while (roots.size()<64 && std::fgets(line,sizeof line,f)) {
            std::string root=trim(line);
            if (!root.empty() && root[0]=='/' && root.find("..") == std::string::npos) roots.push_back(root);
        }
        std::fclose(f);
    }
    std::sort(roots.begin(),roots.end());roots.erase(std::unique(roots.begin(),roots.end()),roots.end());
    return roots;
}

inline std::string clock(double seconds)
{
    const long s = long(std::max(0.0, seconds));
    char buf[24];
    std::snprintf(buf, sizeof buf, "%ld:%02ld", s / 60, s % 60);
    return buf;
}
} // namespace music
