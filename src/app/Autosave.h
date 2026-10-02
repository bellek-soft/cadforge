#pragma once
// Periodic autosave of the open document and recovery after a crash.
//
// While the document has unsaved changes it is written every N minutes to
// configDir()/autosave/<session>.cfp (with the original path and time). The
// file is removed after a successful save and on a clean exit, so any file
// left there at startup belongs to a session that ended unexpectedly.

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace cf::app {

class AppContext;

class Autosave {
public:
    struct Recovery {
        std::filesystem::path file;
        std::string originalPath; // empty for untitled documents
        std::string time;         // local time of the autosave
        std::size_t features = 0;
    };

    Autosave();

    bool enabled = true;

    /// Call once per frame; writes when due (interval from the preferences).
    void tick(AppContext& ctx, double nowSeconds);
    /// Writes the document now (if it has unsaved changes). Returns true on success.
    bool writeNow(AppContext& ctx);
    /// Removes this session's autosave file.
    void clear();

    /// Autosave files of earlier sessions (newest first).
    static std::vector<Recovery> findRecoveries();
    /// Loads a recovery into the context (keeps it marked as modified).
    static bool restore(AppContext& ctx, const Recovery& r);
    static void discard(const Recovery& r);

    static std::filesystem::path directory();

private:
    std::filesystem::path m_file;
    double m_lastWrite = 0.0;
    std::uint64_t m_lastHash = 0;
};

} // namespace cf::app
