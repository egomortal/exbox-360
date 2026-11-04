// The Music Library and player: tags, file names, decoding, resampling, the
// ring between the threads, and the player moving through a list.
// Run from bigapp (make test) so tests/fixtures/music is found.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "../music.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <thread>
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

static std::string fixtures = "tests/fixtures/music";

static void file_names()
{
    using music::title_from_file;
    CHECK(title_from_file("/data/nxe/music/03 - Still Alive.mp3") == "Still Alive");
    CHECK(title_from_file("03. Still Alive.flac") == "Still Alive");
    CHECK(title_from_file("1-03 Song.ogg") == "Song");
    CHECK(title_from_file("my_song.wav") == "my song");
    CHECK(title_from_file("2001 A Space Odyssey.mp3") == "2001 A Space Odyssey"); // four digits: a name, not a track
    CHECK(title_from_file("07.mp3") == "07");                                     // nothing after the number
    CHECK(title_from_file(".mp3") == ".mp3" || !title_from_file(".mp3").empty());
    CHECK(music::playable("a/B.MP3") && music::playable("x.flac") && music::playable("y.ogg") && music::playable("z.wav"));
    CHECK(!music::playable("notes.txt") && !music::playable("music.mp3/") && !music::playable("folder.mp3/readme"));
    CHECK(music::clock(0) == "0:00" && music::clock(61.9) == "1:01" && music::clock(3725) == "62:05");
}

static std::vector<unsigned char> id3(int version, const std::vector<std::vector<unsigned char>> &frames)
{
    std::vector<unsigned char> body;
    for (const auto &f : frames)
        body.insert(body.end(), f.begin(), f.end());
    const std::size_t n = body.size();
    std::vector<unsigned char> tag = {'I', 'D', '3', (unsigned char)version, 0, 0,
                                      (unsigned char)((n >> 21) & 127), (unsigned char)((n >> 14) & 127),
                                      (unsigned char)((n >> 7) & 127), (unsigned char)(n & 127)};
    tag.insert(tag.end(), body.begin(), body.end());
    return tag;
}

static std::vector<unsigned char> frame(int version, const char *id, const std::vector<unsigned char> &data)
{
    const std::size_t n = data.size();
    std::vector<unsigned char> f(id, id + (version == 2 ? 3 : 4));
    if (version == 2)
        f.insert(f.end(), {(unsigned char)(n >> 16), (unsigned char)(n >> 8), (unsigned char)n});
    else if (version == 4)
        f.insert(f.end(), {(unsigned char)((n >> 21) & 127), (unsigned char)((n >> 14) & 127), (unsigned char)((n >> 7) & 127),
                           (unsigned char)(n & 127), 0, 0});
    else
        f.insert(f.end(), {(unsigned char)(n >> 24), (unsigned char)(n >> 16), (unsigned char)(n >> 8), (unsigned char)n, 0, 0});
    f.insert(f.end(), data.begin(), data.end());
    return f;
}

static void tags()
{
    // ID3v2.3: Latin-1 title (with an e-acute), UTF-16 artist with a byte-order mark.
    music::Song s;
    music::parse_id3v2(id3(3, {frame(3, "TIT2", {0, 'C', 'a', 'f', 0xE9, 0}),
                               frame(3, "TPE1", {1, 0xFF, 0xFE, 'A', 0, 'B', 0, 0, 0}),
                               frame(3, "TALB", {3, 'L', 'P'})}),
                       s);
    CHECK(s.title == "Caf\xC3\xA9");
    CHECK(s.artist == "AB");
    CHECK(s.album == "LP");
    // ID3v2.4 with a UTF-8 title and a big-endian UTF-16 artist; v2.2's short ids.
    music::Song t;
    music::parse_id3v2(id3(4, {frame(4, "TIT2", {3, 0xC3, 0xBC, 'x'}), frame(4, "TPE1", {2, 0, 'Z', 0, 'z'})}), t);
    CHECK(t.title == "\xC3\xBCx" && t.artist == "Zz");
    music::Song u;
    music::parse_id3v2(id3(2, {frame(2, "TT2", {0, 'O', 'l', 'd'}), frame(2, "TP1", {0, 'B', 'a', 'n', 'd'})}), u);
    CHECK(u.title == "Old" && u.artist == "Band");
    // A frame claiming more than the tag holds stops the walk without reading past it.
    music::Song bad;
    auto broken = id3(3, {frame(3, "TIT2", {0, 'O', 'K'})});
    broken.insert(broken.end(), {'T', 'P', 'E', '1', 0x7F, 0, 0, 0, 0, 0});
    broken[9] = (unsigned char)(broken.size() - 10);
    music::parse_id3v2(broken, bad);
    CHECK(bad.title == "OK" && bad.artist.empty());
    // UTF-8 that is really Latin-1 is shown as Latin-1.
    CHECK(music::valid_utf8("caf\xE9") == "caf\xC3\xA9");
    CHECK(music::valid_utf8("\xF0\x9F\x8E\xB5 ok") == "\xF0\x9F\x8E\xB5 ok");
    // Vorbis comments: keys in any case.
    const unsigned char vc[] = {3, 0, 0, 0, 'v', 'e', 'n', 2, 0, 0, 0, 7, 0, 0, 0, 't', 'i', 't', 'l', 'e', '=', 'X',
                                8, 0, 0, 0, 'A', 'R', 'T', 'I', 'S', 'T', '=', 'Y'};
    music::Song v;
    music::parse_vorbis_comments(vc, sizeof vc, v);
    CHECK(v.title == "X" && v.artist == "Y");
    // Genres: ID3's TCON (numbers from ID3v1, in brackets or not) and Vorbis GENRE.
    music::Song g;
    music::parse_id3v2(id3(3, {frame(3, "TCON", {0, '(', '1', '7', ')'})}), g);
    CHECK(g.genre == "Rock");
    music::Song g2;
    music::parse_id3v2(id3(4, {frame(4, "TCON", {3, 'S', 'y', 'n', 't', 'h', 'w', 'a', 'v', 'e'})}), g2);
    CHECK(g2.genre == "Synthwave");
    music::Song g3;
    music::parse_id3v2(id3(2, {frame(2, "TCO", {0, '(', '3', '6', ')', 'G', 'a', 'm', 'e', 's'})}), g3);
    CHECK(g3.genre == "Games");
    CHECK(music::genre_name("24") == "Soundtrack" && music::genre_name("(RX)") == "Remix" && music::genre_name("(200)") == "200");
    CHECK(music::genre_name("  Jazz ") == "Jazz" && music::genre_name("").empty() && music::genre_name("1999") == "1999");
    const unsigned char vg[] = {0, 0, 0, 0, 1, 0, 0, 0, 10, 0, 0, 0, 'g', 'e', 'n', 'r', 'e', '=', 'F', 'o', 'l', 'k'};
    music::Song w;
    music::parse_vorbis_comments(vg, sizeof vg, w);
    CHECK(w.genre == "Folk" && w.title.empty());
    // The fixtures, as ffmpeg tagged them.
    music::Song mp3{fixtures + "/Album/01 - track one.mp3", "", "", "", ""};
    music::read_tags(mp3);
    CHECK(mp3.title == "Still Alive" && mp3.artist == "Aperture Choir" && mp3.album == "Test Songs");
    music::Song flac{fixtures + "/Album/02 - track two.flac", "", "", "", ""};
    music::read_tags(flac);
    CHECK(flac.title == "Fl\xC3\xA4" "che" && flac.artist == "\xC3\x9Cn\xC3\xAF" "code Band");
    music::Song ogg{fixtures + "/03_ogg_song.ogg", "", "", "", ""};
    music::read_tags(ogg);
    CHECK(ogg.title == "Vorbis Song" && ogg.artist == "Ogg Artist");
    music::Song wav{fixtures + "/04 Plain Wave.wav", "", "", "", ""};
    music::read_tags(wav);
    CHECK(wav.title == "Plain Wave" && wav.artist.empty());
}

// Decodes a whole file; returns frames and fills the left channel.
static std::size_t decode_all(const std::string &path, music::Decoder &d, std::vector<std::int16_t> &left)
{
    std::vector<std::int16_t> buf(1024 * 8);
    std::size_t total = 0, got;
    while ((got = d.read(buf.data(), 1024)) > 0)
    {
        for (std::size_t i = 0; i < got; ++i)
            left.push_back(buf[i * d.channels()]);
        total += got;
        if (total > 10000000)
            break;
    }
    (void)path;
    return total;
}

// Sign changes per second of a signal: about 880 for a 440 Hz tone.
static double crossings_per_second(const std::vector<std::int16_t> &x, unsigned rate)
{
    int n = 0;
    for (std::size_t i = 1; i < x.size(); ++i)
        if ((x[i - 1] < 0) != (x[i] < 0))
            ++n;
    return x.empty() ? 0 : n * double(rate) / double(x.size());
}

static void decoding()
{
    struct Want
    {
        const char *file;
        unsigned channels, rate;
        double seconds;
        bool length_known;
    };
    const Want wants[] = {{"/Album/01 - track one.mp3", 2, 44100, 0.6, false},
                          {"/Album/02 - track two.flac", 1, 48000, 0.5, true},
                          {"/03_ogg_song.ogg", 2, 44100, 0.5, true},
                          {"/04 Plain Wave.wav", 1, 22050, 0.5, true}};
    for (const auto &w : wants)
    {
        music::Decoder d;
        CHECK(d.open(fixtures + w.file));
        CHECK(d.channels() == w.channels && d.rate() == w.rate);
        CHECK((d.length() > 0) == w.length_known);
        std::vector<std::int16_t> left;
        const std::size_t frames = decode_all(w.file, d, left);
        const double seconds = double(frames) / w.rate;
        CHECK(seconds > w.seconds - 0.02 && seconds < w.seconds + 0.08); // MP3 adds a little padding
        const double cps = crossings_per_second(left, w.rate);
        CHECK(cps > 820 && cps < 940);
        if (w.length_known)
            CHECK(d.length() == frames);
    }
    // A FLAC with an ID3v2 tag in front (some taggers and tools add one) still plays.
    {
        std::vector<unsigned char> flac;
        if (FILE *f = std::fopen((fixtures + "/Album/02 - track two.flac").c_str(), "rb"))
        {
            int c;
            while ((c = std::fgetc(f)) != EOF)
                flac.push_back((unsigned char)c);
            std::fclose(f);
        }
        std::vector<unsigned char> tagged = id3(4, {frame(4, "TIT2", {3, 'F', 'r', 'o', 'n', 't'})});
        tagged.insert(tagged.end(), flac.begin(), flac.end());
        const std::string path = "build/test-id3-front.flac";
        if (FILE *f = std::fopen(path.c_str(), "wb"))
        {
            std::fwrite(tagged.data(), 1, tagged.size(), f);
            std::fclose(f);
        }
        music::Decoder d;
        CHECK(d.open(path) && d.rate() == 48000 && d.length() == 24000);
        std::vector<std::int16_t> left;
        CHECK(decode_all(path, d, left) == 24000);
        music::Song s{path, "", "", "", ""};
        music::read_tags(s);
        CHECK(s.title == "Front");
    }
    music::Decoder bad;
    CHECK(!bad.open(fixtures + "/05 broken.mp3"));
    CHECK(!bad.open(fixtures + "/notes.txt"));
    CHECK(!bad.open(fixtures + "/missing.flac"));
    CHECK(!bad.is_open() && bad.read(nullptr, 10) == 0);
}

static void resampling()
{
    // A 440 Hz tone at 44.1 kHz becomes the same tone at 48 kHz, and pushing it
    // in odd-sized pieces gives the same samples as pushing it all at once.
    std::vector<std::int16_t> in;
    for (int i = 0; i < 44100; ++i)
    {
        const auto v = std::int16_t(12000 * std::sin(2 * M_PI * 440 * i / 44100.0));
        in.push_back(v);
        in.push_back(std::int16_t(-v));
    }
    music::Resampler whole, pieces;
    whole.reset(44100);
    pieces.reset(44100);
    std::vector<std::int16_t> a, b;
    whole.push(in.data(), in.size() / 2, a);
    for (std::size_t at = 0, n = 1; at < in.size() / 2; at += n, n = n * 7 % 997 + 1)
        pieces.push(in.data() + at * 2, std::min(n, in.size() / 2 - at), b);
    CHECK(a.size() / 2 >= 47990 && a.size() / 2 <= 48000);
    CHECK(a.size() == b.size());
    int worst = 0;
    for (std::size_t i = 0; i < std::min(a.size(), b.size()); ++i)
        worst = std::max(worst, std::abs(a[i] - b[i]));
    CHECK(worst <= 1);
    std::vector<std::int16_t> left;
    for (std::size_t i = 0; i < a.size(); i += 2)
        left.push_back(a[i]);
    const double cps = crossings_per_second(left, 48000);
    CHECK(cps > 870 && cps < 890);
    CHECK(a[1] == -a[0] || std::abs(a[1] + a[0]) <= 1); // channels kept apart
    // 48 kHz passes straight through.
    music::Resampler same;
    same.reset(48000);
    std::vector<std::int16_t> c;
    same.push(in.data(), 100, c);
    CHECK(c.size() == 200 && c[10] == in[10]);
    // Upsampling 22.05 kHz roughly doubles the frames.
    music::Resampler up;
    up.reset(22050);
    std::vector<std::int16_t> d;
    up.push(in.data(), 22050, d);
    CHECK(d.size() / 2 >= 47990 && d.size() / 2 <= 48000);
}

static void ring()
{
    music::Ring r;
    CHECK(r.space() == music::Ring::kFrames && r.queued() == 0);
    std::vector<std::int16_t> block(2 * 1000);
    for (std::size_t i = 0; i < block.size(); ++i)
        block[i] = std::int16_t(i % 2 ? -int(i / 2) : int(i / 2));
    // Fill it, wrapping round several times, and read it back in odd amounts.
    std::size_t written = 0, read = 0;
    bool ok = true;
    std::vector<int> acc;
    for (int round = 0; round < 400; ++round)
    {
        written += r.write(block.data(), 1000);
        const std::size_t want = 333 + std::size_t(round % 5) * 101;
        acc.assign(want * 2, 0);
        const std::size_t got = r.mix_into(acc.data(), want, 256);
        for (std::size_t i = 0; i < got; ++i, ++read)
            ok = ok && acc[i * 2] == int(read % 1000) && acc[i * 2 + 1] == -int(read % 1000);
    }
    CHECK(ok);
    CHECK(r.written() == written && r.consumed() == read && r.queued() == written - read);
    // A full ring takes no more.
    while (r.write(block.data(), 1000) > 0)
    {
    }
    CHECK(r.space() == 0 && r.queued() == music::Ring::kFrames);
    // Volume scales; clear() drops what is queued.
    acc.assign(2, 0);
    r.mix_into(acc.data(), 1, 128);
    CHECK(std::abs(acc[0]) <= 500);
    r.clear();
    CHECK(r.queued() == 0 && r.space() == music::Ring::kFrames);
}

// Plays the player out through mix() in audio-callback-sized pieces, pumping
// in between; records which song was current after each piece.
static std::vector<int> play_out(music::Player &p, std::size_t limit_frames)
{
    std::vector<int> seen;
    std::vector<int> acc(1024 * 2);
    for (std::size_t heard = 0; heard < limit_frames; heard += 1024)
    {
        p.pump();
        acc.assign(acc.size(), 0);
        p.mix(acc.data(), 1024);
        const int c = p.current();
        if (seen.empty() || seen.back() != c)
            seen.push_back(c);
        if (c < 0)
            break;
    }
    return seen;
}

static void player()
{
    std::map<std::string, music::Song> known;
    const std::vector<music::Song> songs = music::scan({fixtures, fixtures + "/does-not-exist"}, known);
    // Folder order: "03_ogg...", "04 Plain...", "05 broken", "Album/01", "Album/02" (case-folded path order).
    CHECK(songs.size() == 5);
    CHECK(known.size() == 5);
    std::vector<std::string> titles;
    for (const auto &s : songs)
        titles.push_back(s.title);
    CHECK(titles.size() == 5 && titles[0] == "Vorbis Song" && titles[1] == "Plain Wave" && titles[2] == "broken" &&
          titles[3] == "Still Alive" && titles[4] == "Fl\xC3\xA4" "che");
    // A second look reuses what it read.
    known[songs[1].path].title = "Cached";
    CHECK(music::scan({fixtures}, known)[1].title == "Cached");

    music::Player p;
    CHECK(!p.active() && p.current() == -1 && p.current_song() == nullptr);
    // From "Plain Wave": it, then the broken file is skipped, then the album.
    p.play(songs, 1);
    CHECK(p.current() == 1 && p.current_song() && p.current_song()->title == "Plain Wave");
    CHECK(std::fabs(p.length_seconds() - 0.5) < 0.01);
    const std::vector<int> order = play_out(p, 48000 * 5);
    CHECK((order == std::vector<int>{1, 3, 4, -1}));
    CHECK(!p.active() && p.list().empty());

    // Paused: nothing is heard and nothing is used up.
    p.play(songs, 3);
    p.pump();
    p.set_paused(true);
    std::vector<int> acc(2048, 0);
    p.mix(acc.data(), 1024);
    CHECK(p.elapsed_seconds() == 0);
    bool silent = true;
    for (int v : acc)
        silent = silent && v == 0;
    CHECK(silent);
    p.set_paused(false);
    p.mix(acc.data(), 1024);
    CHECK(p.elapsed_seconds() > 0.02 && p.elapsed_seconds() < 0.03);
    bool sound = false;
    for (int v : acc)
        sound = sound || v != 0;
    CHECK(sound);
    // Next: the album's second song straight away; previous early in a song
    // goes back one, later restarts it.
    p.next();
    CHECK(p.current() == 4 && p.elapsed_seconds() == 0);
    p.previous();
    CHECK(p.current() == 3);
    p.pump();
    for (int i = 0; i < 4; ++i)
    {
        p.pump();
        p.mix(acc.data(), 1024);
    }
    // Early in a song previous goes back one (over the broken file, to Plain
    // Wave); next steps forward over it again.
    p.previous();
    CHECK(p.current() == 1);
    p.next();
    CHECK(p.current() == 3);
    // Next on the last song stops.
    p.next();
    CHECK(p.current() == 4);
    p.next();
    CHECK(!p.active());
    // Previous going back over the broken file lands on the one before it.
    p.play(songs, 3);
    p.previous();
    CHECK(p.current() == 1);
    p.stop();
    CHECK(!p.active() && p.list().empty());
    // A list where nothing opens plays nothing.
    p.play({songs[2]}, 0);
    CHECK(!p.active());
    p.pump();
    CHECK(!p.active());
    // Not even when repeating.
    p.set_repeat(music::Repeat::All);
    p.play({songs[2]}, 0);
    p.pump();
    CHECK(!p.active());
    p.set_repeat(music::Repeat::Off);
}

static std::vector<std::string> titles_of(const std::vector<music::Song> &list)
{
    std::vector<std::string> out;
    for (const auto &s : list)
        out.push_back(s.title);
    return out;
}

// Repeat (one song, or the list) and shuffle, as the 360's player had.
static void shuffle_and_repeat()
{
    std::map<std::string, music::Song> known;
    const std::vector<music::Song> songs = music::scan({fixtures}, known);
    const std::vector<std::string> natural = titles_of(songs);
    music::Player p;
    CHECK(p.repeat() == music::Repeat::Off && !p.shuffle());

    // Repeat one: the same song over and over (Plain Wave is half a second).
    p.set_repeat(music::Repeat::One);
    p.play(songs, 1);
    CHECK((play_out(p, 48000 * 2) == std::vector<int>{1}));
    CHECK(p.current() == 1 && p.elapsed_seconds() < 0.5);
    // Next still moves on, and from the last song goes round to the first.
    p.next();
    CHECK(p.current() == 3);
    p.play(songs, 4);
    p.next();
    CHECK(p.current() == 0);

    // Repeat all: after the last song the first follows, without a stop.
    p.set_repeat(music::Repeat::All);
    p.play(songs, 3);
    const std::vector<int> round = play_out(p, 48000 * 12);
    CHECK(round.size() >= 5 && (std::vector<int>(round.begin(), round.begin() + 5) == std::vector<int>{3, 4, 0, 1, 3}));
    CHECK(p.active());
    // Early in the first song, previous goes round to the last.
    p.play(songs, 0);
    p.previous();
    CHECK(p.current() == 4);
    p.next();
    CHECK(p.current() == 0);
    p.set_repeat(music::Repeat::Off);
    p.stop();

    // Shuffled: the chosen song first, then every other one once, in some order.
    p.set_shuffle(true, 12345);
    CHECK(p.shuffle());
    p.play(songs, 1);
    CHECK(p.current() == 0 && p.current_song() && p.current_song()->title == "Plain Wave");
    std::vector<std::string> mixed = titles_of(p.list()), sorted_mixed = mixed, sorted_natural = natural;
    std::sort(sorted_mixed.begin(), sorted_mixed.end());
    std::sort(sorted_natural.begin(), sorted_natural.end());
    CHECK(mixed.size() == 5 && mixed[0] == "Plain Wave" && sorted_mixed == sorted_natural);
    // Another seed, another order (the four after the first: 24 orders).
    music::Player q;
    q.set_shuffle(true, 99);
    q.play(songs, 1);
    music::Player r;
    r.set_shuffle(true, 7);
    r.play(songs, 1);
    CHECK(titles_of(q.list()) != mixed || titles_of(r.list()) != mixed);
    // Shuffle off mid-song: the list's own order again, the song plays on.
    p.pump();
    std::vector<int> acc(2048, 0);
    p.mix(acc.data(), 1024);
    const double heard = p.elapsed_seconds();
    p.set_shuffle(false);
    CHECK(!p.shuffle() && titles_of(p.list()) == natural);
    CHECK(p.current() == 1 && p.current_song()->title == "Plain Wave" && p.elapsed_seconds() == heard);
    CHECK((play_out(p, 48000 * 5) == std::vector<int>{1, 3, 4, -1}));
    // Shuffle on mid-song: what plays stays first; the rest each play once.
    p.play(songs, 0);
    p.pump();
    p.set_shuffle(true, 4242);
    CHECK(p.current() == 0 && p.current_song()->title == "Vorbis Song");
    mixed = titles_of(p.list());
    sorted_mixed = mixed;
    std::sort(sorted_mixed.begin(), sorted_mixed.end());
    CHECK(mixed[0] == "Vorbis Song" && sorted_mixed == sorted_natural);
    const std::vector<int> once = play_out(p, 48000 * 12);
    std::vector<int> heard_songs(once.begin(), once.end() - 1);
    std::sort(heard_songs.begin(), heard_songs.end());
    CHECK(once.back() == -1 && heard_songs.size() == 4 &&
          std::adjacent_find(heard_songs.begin(), heard_songs.end()) == heard_songs.end());
    // Shuffle and repeat-all together keep going round the shuffled list.
    p.set_repeat(music::Repeat::All);
    p.play(songs, 3);
    const std::vector<int> shuffled_round = play_out(p, 48000 * 12);
    CHECK(shuffled_round.size() >= 5 && shuffled_round[0] == 0 && p.active());
}

// The audio thread consuming while the main thread decodes (for ThreadSanitizer).
static void threads()
{
    std::map<std::string, music::Song> known;
    std::vector<music::Song> songs = music::scan({fixtures}, known);
    music::Player p;
    p.play(songs, 0);
    std::atomic<bool> done{false};
    std::atomic<long> mixed{0};
    std::thread audio([&] {
        std::vector<int> acc(512 * 2);
        while (!done.load())
        {
            p.mix(acc.data(), 512);
            mixed += 512;
            std::this_thread::yield();
        }
    });
    for (int i = 0; i < 2000 && p.active(); ++i)
    {
        p.pump(4096);
        std::this_thread::yield();
    }
    done = true;
    audio.join();
    CHECK(mixed.load() > 0);
}

static void write_bytes(const std::string &path, const std::vector<unsigned char> &b)
{
    if (FILE *f = std::fopen(path.c_str(), "wb")) { std::fwrite(b.data(), 1, b.size(), f); std::fclose(f); }
}
static void cover_art()
{
    const std::string dir = "/tmp/exbox-cover-test";
    if (std::system(("rm -rf " + dir + " && mkdir -p " + dir + "/album " + dir + "/art").c_str()) != 0) { CHECK(false); return; }
    const std::vector<unsigned char> image = {0xFF, 0xD8, 0xFF, 0xE0, 'J', 'P', 'G', 0xFF, 0xD9};
    // ID3v2.3 with an APIC frame: encoding, MIME, type 3 (front), description, data.
    std::vector<unsigned char> frame = {0};
    for (char c : std::string("image/jpeg")) frame.push_back(static_cast<unsigned char>(c));
    frame.push_back(0); frame.push_back(3);
    for (char c : std::string("Front")) frame.push_back(static_cast<unsigned char>(c));
    frame.push_back(0);
    frame.insert(frame.end(), image.begin(), image.end());
    std::vector<unsigned char> tag = {'I', 'D', '3', 3, 0, 0};
    const std::size_t total = 10 + frame.size();
    tag.push_back((total >> 21) & 0x7F); tag.push_back((total >> 14) & 0x7F); tag.push_back((total >> 7) & 0x7F); tag.push_back(total & 0x7F);
    for (char c : std::string("APIC")) tag.push_back(static_cast<unsigned char>(c));
    tag.push_back(0); tag.push_back(0); tag.push_back((frame.size() >> 8) & 0xFF); tag.push_back(frame.size() & 0xFF); tag.push_back(0); tag.push_back(0);
    tag.insert(tag.end(), frame.begin(), frame.end());
    std::vector<unsigned char> mp3 = tag;
    for (int i = 0; i < 64; ++i) mp3.push_back(0);
    write_bytes(dir + "/embedded.mp3", mp3);
    CHECK(music::cover_art(dir + "/embedded.mp3", dir + "/art") == image);
    // ID3v2.2 PIC: encoding, three-letter format, type, description, data.
    std::vector<unsigned char> pic = {0, 'J', 'P', 'G', 3, 0};
    pic.insert(pic.end(), image.begin(), image.end());
    std::vector<unsigned char> v22 = {'I', 'D', '3', 2, 0, 0, 0, 0, 0, static_cast<unsigned char>(6 + pic.size()), 'P', 'I', 'C', 0, 0, static_cast<unsigned char>(pic.size())};
    v22.insert(v22.end(), pic.begin(), pic.end());
    write_bytes(dir + "/old.mp3", v22);
    CHECK(music::cover_art(dir + "/old.mp3", dir + "/art") == image);
    // FLAC: STREAMINFO-sized dummy block, then a PICTURE block (type 3).
    std::vector<unsigned char> block;
    auto be32 = [&](std::uint32_t v) { for (int s = 24; s >= 0; s -= 8) block.push_back((v >> s) & 0xFF); };
    be32(3); be32(10); for (char c : std::string("image/jpeg")) block.push_back(static_cast<unsigned char>(c));
    be32(0); be32(1); be32(1); be32(24); be32(0); be32(std::uint32_t(image.size()));
    block.insert(block.end(), image.begin(), image.end());
    std::vector<unsigned char> flac = {'f', 'L', 'a', 'C', 0, 0, 0, 34};
    for (int i = 0; i < 34; ++i) flac.push_back(0);
    flac.push_back(0x80 | 6); flac.push_back((block.size() >> 16) & 0xFF); flac.push_back((block.size() >> 8) & 0xFF); flac.push_back(block.size() & 0xFF);
    flac.insert(flac.end(), block.begin(), block.end());
    write_bytes(dir + "/song.flac", flac);
    CHECK(music::cover_art(dir + "/song.flac", dir + "/art") == image);
    // The owner's own picture beats the embedded one; the folder's is the last resort.
    const std::vector<unsigned char> mine = {0x89, 'P', 'N', 'G', 1};
    write_bytes(dir + "/art/embedded.png", mine);
    CHECK(music::cover_art(dir + "/embedded.mp3", dir + "/art") == mine);
    const std::vector<unsigned char> folder = {0xFF, 0xD8, 'F', 'O', 'L', 'D'};
    write_bytes(dir + "/album/plain.mp3", std::vector<unsigned char>(64, 0));
    CHECK(music::cover_art(dir + "/album/plain.mp3", dir + "/art").empty());
    write_bytes(dir + "/album/folder.jpg", folder);
    CHECK(music::cover_art(dir + "/album/plain.mp3", dir + "/art") == folder);
    CHECK(music::base64("aGVsbG8=") == std::vector<unsigned char>({'h', 'e', 'l', 'l', 'o'}));
}

// The current playlist edited while it plays: added to, taken from, reordered.
static void playlist_editing()
{
    std::map<std::string, music::Song> known;
    const std::vector<music::Song> songs = music::scan({fixtures}, known);
    // 0 Vorbis Song, 1 Plain Wave, 2 broken, 3 Still Alive, 4 Flache
    music::Player p;
    // Adding with nothing playing starts the added songs.
    p.append({songs[1]});
    CHECK(p.current() == 0 && p.list().size() == 1);
    // Added while the only song plays (and has been decoded to the end): it follows.
    p.pump(48000 * 4);
    p.append({songs[3], songs[4]});
    CHECK(p.list().size() == 3 && p.current() == 0);
    CHECK((play_out(p, 48000 * 10) == std::vector<int>{0, 1, 2, -1}));
    // Moving the song after the one heard: the new next song follows.
    p.play({songs[1], songs[3], songs[4]}, 0);
    p.pump(48000 * 4); // decoded well ahead into song 1
    p.move(2, 1);      // Flache before Still Alive
    CHECK(titles_of(p.list())[1] == "Fl\xC3\xA4" "che" && p.current() == 0);
    std::vector<int> heard = play_out(p, 48000 * 10);
    CHECK((heard == std::vector<int>{0, 1, 2, -1}));
    // Removing a song ahead of the one heard.
    p.play({songs[1], songs[3], songs[4]}, 0);
    p.pump(48000 * 4);
    p.remove(1);
    CHECK(p.list().size() == 2 && titles_of(p.list())[1] == "Fl\xC3\xA4" "che");
    CHECK((play_out(p, 48000 * 10) == std::vector<int>{0, 1, -1}));
    // Removing the song heard skips to the next; removing the last one heard stops.
    p.play({songs[1], songs[3]}, 0);
    p.remove(0);
    CHECK(p.current() == 0 && p.current_song() && p.current_song()->title == "Still Alive");
    p.remove(0);
    CHECK(!p.active());
    // Jumping plays that song now; paused stays paused.
    p.play({songs[1], songs[3], songs[4]}, 0);
    p.set_paused(true);
    p.jump(2);
    CHECK(p.current() == 2 && p.paused());
    p.set_paused(false);
    p.stop();
}

int main(int argc, char **argv)
{
    if (argc > 1)
        fixtures = argv[1];
    file_names();
    tags();
    decoding();
    resampling();
    ring();
    player();
    shuffle_and_repeat();
    threads();
    playlist_editing();
    cover_art();
    std::printf("%d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
