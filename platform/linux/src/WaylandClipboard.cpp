#include "WaylandClipboard.h"
#include "WaylandWindow.h"  // for WaylandState

#include <wayland-client.h>
#include <cstring>
#include <cstdio>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <errno.h>

namespace termcore {

// ─── Data source listener (for copy) ───────────────────────────────

struct DataSourceContext {
    WaylandClipboard* clipboard;
    std::string* buffer;
};

static void data_source_target(void* /*data*/, wl_data_source* /*source*/,
                               const char* /*mime_type*/) {
    // Target accepts our mime type — no action needed
}

static void data_source_send(void* data, wl_data_source* /*source*/,
                             const char* mime_type, int32_t fd) {
    auto* ctx = static_cast<DataSourceContext*>(data);

    if (strcmp(mime_type, "text/plain;charset=utf-8") == 0 ||
        strcmp(mime_type, "text/plain") == 0) {
        const auto& text = *ctx->buffer;
        size_t written = 0;
        while (written < text.size()) {
            ssize_t n = write(fd, text.data() + written,
                              text.size() - written);
            if (n < 0) {
                if (errno == EINTR) continue;
                break;
            }
            written += static_cast<size_t>(n);
        }
    }
    close(fd);
}

static void data_source_cancelled(void* data, wl_data_source* source) {
    auto* ctx = static_cast<DataSourceContext*>(data);
    wl_data_source_destroy(source);
    delete ctx;
}

static const wl_data_source_listener data_source_listener = {
    .target = data_source_target,
    .send = data_source_send,
    .cancelled = data_source_cancelled,
};

// ─── Data offer listener (for paste) ───────────────────────────────

static void data_offer_offer(void* /*data*/, wl_data_offer* /*offer*/,
                             const char* /*mime_type*/) {
    // The offer advertises available mime types — we track them
    // but for simplicity we always request text/plain;charset=utf-8
}

static const wl_data_offer_listener data_offer_listener = {
    .offer = data_offer_offer,
};

// ─── WaylandClipboard implementation ───────────────────────────────

WaylandClipboard::WaylandClipboard() = default;

WaylandClipboard::~WaylandClipboard() {
    if (current_offer_) {
        wl_data_offer_destroy(current_offer_);
    }
    // current_source_ is destroyed via the cancelled callback
}

void WaylandClipboard::init(WaylandState* state) {
    state_ = state;
}

void WaylandClipboard::copy(const std::string& text) {
    if (!state_ || !state_->data_device_manager || !state_->data_device) {
        return;
    }

    // Create a new data source
    wl_data_source* source =
        wl_data_device_manager_create_data_source(
            state_->data_device_manager);
    if (!source) return;

    // Store the text buffer
    copy_buffer_ = text;

    // Create context for callbacks (ownership transferred to cancelled cb)
    auto* ctx = new DataSourceContext{this, &copy_buffer_};

    wl_data_source_add_listener(source, &data_source_listener, ctx);
    wl_data_source_offer(source, "text/plain;charset=utf-8");
    wl_data_source_offer(source, "text/plain");

    // Set as the current selection.
    // Serial 0 is used here; in a real implementation the serial from
    // the last input event should be passed.
    wl_data_device_set_selection(state_->data_device, source, 0);

    current_source_ = source;
}

std::string WaylandClipboard::paste() {
    if (!current_offer_) return "";

    // Create a pipe to receive data
    int fds[2];
    if (pipe(fds) != 0) {
        return "";
    }

    // Request the data in text/plain;charset=utf-8
    wl_data_offer_receive(current_offer_,
                          "text/plain;charset=utf-8", fds[1]);
    close(fds[1]);

    // Flush to ensure the request reaches the compositor
    if (state_ && state_->display) {
        wl_display_flush(state_->display);
    }

    // Read the data from the pipe
    std::string result;
    char buf[4096];

    // Use poll to avoid blocking forever
    struct pollfd pfd = {
        .fd = fds[0],
        .events = POLLIN,
        .revents = 0,
    };

    while (true) {
        int ready = poll(&pfd, 1, 500); // 500ms timeout
        if (ready <= 0) break;

        ssize_t n = read(fds[0], buf, sizeof(buf));
        if (n <= 0) break;
        result.append(buf, static_cast<size_t>(n));
    }

    close(fds[0]);
    return result;
}

void WaylandClipboard::setCurrentOffer(wl_data_offer* offer) {
    if (offer) {
        wl_data_offer_add_listener(offer, &data_offer_listener, this);
    }
}

void WaylandClipboard::onSelectionOffer(wl_data_offer* offer) {
    if (current_offer_ && current_offer_ != offer) {
        wl_data_offer_destroy(current_offer_);
    }
    current_offer_ = offer;
}

} // namespace termcore
