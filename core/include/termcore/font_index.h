#ifndef TERMCORE_FONT_INDEX_H
#define TERMCORE_FONT_INDEX_H

#include <string>
#include <vector>
#include <functional>

namespace termcore {

struct FontMetadata {
    std::string name;
    std::string postscript_name;
    std::string category;          // "monospace", "variable", etc.
    bool has_ligatures = false;
    bool has_nerd_font_variant = false;
    std::string download_url;
    std::string nerd_font_download_url;
    std::string license;
    bool installed = false;
};

class FontIndex {
public:
    bool loadFromJSON(const std::string& json);

    const std::vector<FontMetadata>& all() const;
    size_t count() const;

    /// Case-insensitive substring search on font name.
    std::vector<const FontMetadata*> search(const std::string& query) const;

    /// Filter by boolean flags. Pass false to ignore a criterion.
    std::vector<const FontMetadata*> filter(bool installed_only,
                                            bool nerd_fonts_only,
                                            bool ligatures_only) const;

    void markInstalled(const std::string& name);

    /// Inject a platform predicate that checks if a font (by postscript name)
    /// is available on the system.
    void setInstalledPredicate(std::function<bool(const std::string&)> pred);

    /// Re-check installed status for all fonts using the installed predicate.
    void refreshInstallStatus();

private:
    std::vector<FontMetadata> fonts_;
    std::function<bool(const std::string&)> installedPredicate_;
};

} // namespace termcore

#endif
