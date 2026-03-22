#include "termcore/annotations.h"

namespace termcore {

int AnnotationManager::addAnnotation(int absoluteRow, const std::string& text,
                                     int startCol, int endCol,
                                     uint32_t color) {
    int id = nextId_++;
    Annotation ann;
    ann.absoluteRow = absoluteRow;
    ann.startCol = startCol;
    ann.endCol = endCol;
    ann.text = text;
    ann.color = color;
    ann.created = std::chrono::steady_clock::now();
    annotations_.emplace(id, std::move(ann));
    return id;
}

bool AnnotationManager::removeAnnotation(int id) {
    return annotations_.erase(id) > 0;
}

const std::map<int, Annotation>& AnnotationManager::annotations() const {
    return annotations_;
}

std::vector<const Annotation*> AnnotationManager::annotationsInRange(
    int startRow, int endRow) const {
    std::vector<const Annotation*> result;
    for (const auto& [id, ann] : annotations_) {
        if (ann.absoluteRow >= startRow && ann.absoluteRow <= endRow) {
            result.push_back(&ann);
        }
    }
    return result;
}

bool AnnotationManager::hasAnnotation(int absoluteRow) const {
    for (const auto& [id, ann] : annotations_) {
        if (ann.absoluteRow == absoluteRow) return true;
    }
    return false;
}

void AnnotationManager::clear() {
    annotations_.clear();
    nextId_ = 1;
}

size_t AnnotationManager::count() const {
    return annotations_.size();
}

std::string expandBadgeFormat(const std::string& format,
                              const std::string& hostname,
                              const std::string& user,
                              const std::string& shell,
                              const std::string& cwd,
                              const std::string& branch) {
    std::string result = format;

    // Simple iterative replacement for each placeholder
    auto replace = [&](const std::string& placeholder, const std::string& value) {
        size_t pos = 0;
        while ((pos = result.find(placeholder, pos)) != std::string::npos) {
            result.replace(pos, placeholder.size(), value);
            pos += value.size();
        }
    };

    replace("{hostname}", hostname);
    replace("{user}", user);
    replace("{shell}", shell);
    replace("{cwd}", cwd);
    replace("{branch}", branch);

    return result;
}

}  // namespace termcore
