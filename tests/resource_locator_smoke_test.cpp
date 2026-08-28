// pom1::ResourceLocator — one search order for POM1's data.
//
// Every consumer used to re-implement the same "try x, then ../x, then ../../x"
// walk, and they did not agree on how far to climb: Memory::loadROM() went up
// ONE level, the sdcard/disks/cfcard probes TWO, the CodeTank probe THREE. Run
// POM1 from build/tests/ and it found the disk images but not the ROMs, then
// quietly substituted its built-in Woz Monitor and carried on.
//
// Pure enough to test directly: a locator is a value over a root list, and the
// only thing it touches is the filesystem it is pointed at.
//
// Covered:
//   §1  roots are consulted in order, and the first hit wins;
//   §2  a miss is an empty path, not a guess;
//   §3  findDirectory() refuses a file, find() accepts either;
//   §4  an absolute path is an ANSWER, not a question — it is not rewritten;
//   §5  rootedAt() sees nothing outside its root, which is what lets a test say
//       "look ONLY here";
//   §6  a non-existent root is harmless, not fatal;
//   §7  the default locator reaches the repo from the working directory and
//       from its ancestors — the inconsistency this class exists to end.

#include "ResourceLocator.h"

#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using pom1::ResourceLocator;

namespace {

void writeFile(const fs::path& p, const std::string& text)
{
    fs::create_directories(p.parent_path());
    std::ofstream(p) << text;
}

std::string read(const fs::path& p)
{
    std::ifstream in(p);
    std::string s;
    std::getline(in, s);
    return s;
}

} // namespace

int main()
{
    const fs::path sandbox = fs::temp_directory_path() / "pom1_resource_locator_smoke";
    fs::remove_all(sandbox);

    const fs::path first = sandbox / "first";
    const fs::path second = sandbox / "second";
    writeFile(first / "roms" / "shared.rom", "from-first");
    writeFile(second / "roms" / "shared.rom", "from-second");
    writeFile(second / "roms" / "only-second.rom", "second-only");
    fs::create_directories(second / "sdcard");

    // -----------------------------------------------------------------
    // §1 Roots are consulted in order; the first hit wins. That ordering IS
    //    the policy — the working directory before the packaged layouts.
    // -----------------------------------------------------------------
    {
        const ResourceLocator loc({first, second});
        assert(read(loc.find("roms/shared.rom")) == "from-first");
        // ...and a file only the second root has is still found.
        assert(read(loc.find("roms/only-second.rom")) == "second-only");

        const ResourceLocator reversed({second, first});
        assert(read(reversed.find("roms/shared.rom")) == "from-second");
    }

    // -----------------------------------------------------------------
    // §2 A miss is an empty path. Never a plausible-looking path that is not
    //    there — callers test emptiness, and a guess would be opened blind.
    // -----------------------------------------------------------------
    {
        const ResourceLocator loc({first, second});
        assert(loc.find("roms/absent.rom").empty());
        assert(loc.find("").empty());
        assert(!loc.exists("roms/absent.rom"));
        assert(loc.exists("roms/shared.rom"));
    }

    // -----------------------------------------------------------------
    // §3 findDirectory() must refuse a file. Memory asks it for "sdcard" and
    //    hands the answer to MicroSD as a directory to enumerate.
    // -----------------------------------------------------------------
    {
        const ResourceLocator loc({first, second});
        assert(!loc.findDirectory("sdcard").empty());
        assert(loc.findDirectory("roms/shared.rom").empty());   // a file, not a dir
        assert(!loc.find("sdcard").empty());                    // find takes either
        assert(loc.findDirectory("").empty());
    }

    // -----------------------------------------------------------------
    // §4 An absolute path is the caller's answer. `--iec-disk /tmp/x.d64` and
    //    every file picker come through here; rewriting those against the roots
    //    would be actively wrong.
    // -----------------------------------------------------------------
    {
        const ResourceLocator loc({first});
        const fs::path outside = second / "roms" / "only-second.rom";
        assert(loc.find(outside.string()) == outside);
        assert(read(loc.find(outside.string())) == "second-only");
        // ...but an absolute path that is not there is still a miss.
        assert(loc.find((second / "nope").string()).empty());
        // Absolute-and-a-directory obeys the same rule.
        assert(loc.findDirectory((second / "sdcard").string()) == second / "sdcard");
        assert(loc.findDirectory(outside.string()).empty());
    }

    // -----------------------------------------------------------------
    // §5 rootedAt() sees NOTHING else. This is the injection point: chdir'ing
    //    to an empty directory no longer means "no data anywhere", because
    //    resources are also looked for next to the executable — and the test
    //    binary sits inside the repo. Saying "look only here" needs a locator.
    // -----------------------------------------------------------------
    {
        const ResourceLocator confined = ResourceLocator::rootedAt(first);
        assert(confined.roots().size() == 1);
        assert(read(confined.find("roms/shared.rom")) == "from-first");
        assert(confined.find("roms/only-second.rom").empty());

        const fs::path empty = sandbox / "empty";
        fs::create_directories(empty);
        const ResourceLocator nothing = ResourceLocator::rootedAt(empty);
        assert(nothing.find("roms/WozMonitor.rom").empty());
        assert(nothing.findDirectory("sdcard").empty());
    }

    // -----------------------------------------------------------------
    // §6 A root that does not exist is skipped, not fatal. Roots are kept even
    //    when absent because one may be created later — the macOS user-data
    //    directory is provisioned during startup.
    // -----------------------------------------------------------------
    {
        const ResourceLocator loc({sandbox / "does-not-exist", first});
        assert(loc.roots().size() == 2);
        assert(read(loc.find("roms/shared.rom")) == "from-first");
    }

    // -----------------------------------------------------------------
    // §7 The default locator. This test binary runs from the build tree, so
    //    the repo is reachable — the point being that ONE order now serves
    //    every resource, where the old per-site walks climbed different
    //    distances and disagreed about what existed.
    // -----------------------------------------------------------------
    {
        const ResourceLocator loc = ResourceLocator::defaultLocator();
        assert(!loc.roots().empty());
        // No duplicates: cwd ancestors and executable-relative roots overlap in
        // a dev tree, and a duplicated root doubles every miss.
        for (size_t i = 0; i < loc.roots().size(); ++i)
            for (size_t j = i + 1; j < loc.roots().size(); ++j)
                assert(loc.roots()[i] != loc.roots()[j]);

        // Recomputed per call, so it follows a chdir. Caching it made
        // resolution depend on WHEN the locator was first touched.
        const fs::path previous = fs::current_path();
        fs::current_path(sandbox);
        const ResourceLocator moved = ResourceLocator::defaultLocator();
        // Compare canonically: the locator canonicalises its roots (that is
        // what lets it deduplicate them), and on macOS the temp directory lives
        // under /var, which is a symlink to /private/var.
        const fs::path canonicalSandbox = fs::weakly_canonical(sandbox);
        bool sawSandbox = false;
        for (const fs::path& r : moved.roots()) sawSandbox |= (r == canonicalSandbox);
        assert(sawSandbox && "defaultLocator must follow the working directory");
        fs::current_path(previous);
    }

    fs::remove_all(sandbox);
    std::printf("resource_locator_smoke: OK\n");
    return 0;
}
