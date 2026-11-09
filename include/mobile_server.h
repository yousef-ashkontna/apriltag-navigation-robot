#pragma once
#include <ESP8266WiFi.h>
#include "mobile_page.h"
#include "phone_control.h"

// Read and send a little on each pass so a slow phone doesn't hold up motor control.
// Commands use POST query parameters, with no request body.
class MobileServer {
public:
    using StatusCallback = String (*)();
    using ConnectCheck = bool (*)();
    using MaintenanceCallback = bool (*)(bool);

    MobileServer(robot::PhoneControl& control,
                 StatusCallback status,
                 ConnectCheck canConnect,
                 MaintenanceCallback maintenance)
        : control_(control), status_(status), canConnect_(canConnect), maintenance_(maintenance),
          server_(80) {}

    void begin() {
        server_.begin();
        server_.setNoDelay(true);
    }

    void tick(uint32_t now) {
        for (auto& slot : slots_) {
            if (!slot.client) {
                slot.client = server_.accept();
                if (slot.client) {
                    slot.client.setNoDelay(true);
                    slot.client.setTimeout(5);
                    slot.started = slot.progress = now;
                }
            }
            if (!slot.client)
                continue;
            if (now - slot.progress > 600 || now - slot.started > 3000) {
                close(slot);
                continue;
            }
            if (!slot.responding) {
                for (int n = 0; n < 256 && slot.client.available(); ++n) {
                    slot.request += char(slot.client.read());
                    slot.progress = now;
                    if (slot.request.length() > 1024) {
                        close(slot);
                        break;
                    }
                    if (slot.request.endsWith("\r\n\r\n")) {
                        handleRequest(slot, now);
                        break;
                    }
                }
            }
            if (slot.responding)
                sendChunk(slot, now);
        }
    }

private:
    struct Slot {
        WiFiClient client;
        String request, header, body;
        uint32_t started = 0, progress = 0;
        size_t sent = 0;
        bool responding = false, page = false;
    } slots_[4];

    robot::PhoneControl& control_;
    StatusCallback status_;
    ConnectCheck canConnect_;
    MaintenanceCallback maintenance_;
    WiFiServer server_;

    static void close(Slot& connection) {
        connection.client.abort();
        connection.request = "";
        connection.header = "";
        connection.body = "";
        connection.sent = 0;
        connection.responding = connection.page = false;
    }

    static String queryValue(const String& url, const char* name) {
        const int query = url.indexOf('?');
        if (query < 0)
            return "";
        const String key = String(name) + "=";
        int start = query + 1;
        while (start < int(url.length())) {
            int end = url.indexOf('&', start);
            if (end < 0)
                end = url.length();
            if (url.substring(start, start + key.length()) == key)
                return url.substring(start + key.length(), end);
            start = end + 1;
        }
        return "";
    }

    static bool parseUnsigned(const String& text, uint32_t& value) {
        if (!text.length() || text.length() > 10)
            return false;
        uint64_t result = 0;
        for (size_t i = 0; i < text.length(); ++i) {
            if (text[i] < '0' || text[i] > '9')
                return false;
            result = result * 10 + text[i] - '0';
            if (result > UINT32_MAX)
                return false;
        }
        value = uint32_t(result);
        return true;
    }

    static bool parsePercent(const String& text, int& value) {
        const bool negative = text.startsWith("-");
        uint32_t magnitude;
        if (!parseUnsigned(negative ? text.substring(1) : text, magnitude) || magnitude > 100)
            return false;
        value = negative ? -int(magnitude) : int(magnitude);
        return true;
    }

    static void respond(Slot& connection, int code, const String& body, bool page = false) {
        connection.body = body;
        connection.page = page;
        connection.responding = true;
        connection.sent = 0;
        const size_t size = page ? sizeof(MOBILE_PAGE) - 1 : body.length();
        connection.header = "HTTP/1.1 " + String(code) + (code == 200 ? " OK\r\n" : " Error\r\n");
        connection.header += "Content-Type: ";
        connection.header += page ? "text/html; charset=utf-8" : "application/json";
        connection.header += "\r\nCache-Control: no-store\r\nConnection: "
                             "close\r\nX-Content-Type-Options: nosniff\r\nContent-Length: " +
                             String(size) + "\r\n\r\n";
        connection.request = "";
    }

    void handleRequest(Slot& connection, uint32_t now) {
        const int first = connection.request.indexOf(' '),
                  last = connection.request.indexOf(' ', first + 1);
        if (first < 0 || last < 0) {
            respond(connection, 400, "{\"error\":\"Bad request\"}");
            return;
        }
        const String method = connection.request.substring(0, first),
                     url = connection.request.substring(first + 1, last);
        const int queryStart = url.indexOf('?');
        const String path = queryStart < 0 ? url : url.substring(0, queryStart);
        if (method == "GET" && path == "/") {
            respond(connection, 200, "", true);
            return;
        }
        if (method == "GET" && path == "/api/state") {
            respond(connection, 200, status_());
            return;
        }
        if (method != "POST") {
            respond(connection, 404, "{\"error\":\"Not found\"}");
            return;
        }
        if (path == "/api/stop") {
            control_.stop();
            respond(connection, 200, "{\"ok\":true}");
            return;
        }
        uint32_t deadline;
        if (!parseUnsigned(queryValue(url, "deadline"), deadline) ||
            deadline - now > cfg::COMMAND_MAX_AHEAD_MS) {
            respond(connection, 409, "{\"error\":\"Expired command; refresh status\"}");
            return;
        }
        if (path == "/api/claim") {
            if (!canConnect_()) {
                respond(connection,
                        409,
                        "{\"error\":\"Wait for the wired link and finish maintenance first\"}");
                return;
            }
            uint32_t token = os_random();
            if (!token)
                token = 1;
            if (!control_.claim(token, now)) {
                respond(connection, 409, "{\"error\":\"Controls already in use\"}");
                return;
            }
            respond(connection, 200, "{\"token\":" + String(token) + "}");
            return;
        }
        if (path == "/api/maintenance") {
            const String enabled = queryValue(url, "enabled");
            if ((enabled != "0" && enabled != "1") || !maintenance_(enabled == "1")) {
                respond(
                    connection,
                    409,
                    "{\"error\":\"Wait for the camera link and any current upload to finish\"}");
                return;
            }
            respond(connection, 200, "{\"ok\":true}");
            return;
        }
        if (path != "/api/control") {
            respond(connection, 404, "{\"error\":\"Not found\"}");
            return;
        }
        uint32_t token, sequence;
        if (!parseUnsigned(queryValue(url, "token"), token) ||
            !parseUnsigned(queryValue(url, "seq"), sequence) ||
            !control_.authorize(token, sequence, deadline, now)) {
            respond(
                connection, 409, "{\"error\":\"Control session expired or command out of order\"}");
            return;
        }
        const String action = queryValue(url, "action");
        bool ok = true;
        if (action == "auto") {
            uint32_t tag;
            ok = parseUnsigned(queryValue(url, "tag"), tag) && tag < cfg::TAG_COUNT &&
                 control_.navigate(tag);
        } else if (action == "manual")
            ok = control_.manual();
        else if (action == "idle") {
            control_.command.mode = robot::Mode::IDLE;
            ++control_.command.requestId;
        } else if (action == "drive") {
            int forward, turn;
            ok = parsePercent(queryValue(url, "v"), forward) &&
                 parsePercent(queryValue(url, "w"), turn) && control_.drive(forward, turn, now);
        } else if (action != "heartbeat")
            ok = false;
        if (!ok) {
            control_.stop();
            respond(connection, 400, "{\"error\":\"Invalid command\"}");
            return;
        }
        respond(connection, 200, "{\"ok\":true}");
    }

    static void sendChunk(Slot& connection, uint32_t now) {
        const size_t headerSize = connection.header.length(),
                     bodySize =
                         connection.page ? sizeof(MOBILE_PAGE) - 1 : connection.body.length();
        const size_t total = headerSize + bodySize;
        if (connection.sent >= total) {
            // stop(0) uses the default timeout, so pass 1 to keep the wait short.
            connection.client.stop(1);
            connection.request = "";
            connection.header = "";
            connection.body = "";
            connection.sent = 0;
            connection.responding = connection.page = false;
            return;
        }
        int room = connection.client.availableForWrite();
        if (room <= 0)
            return;
        size_t n = total - connection.sent;
        if (n > 256)
            n = 256;
        if (n > size_t(room))
            n = room;
        uint8_t buffer[256];
        for (size_t i = 0; i < n; ++i) {
            const size_t at = connection.sent + i;
            buffer[i] = at < headerSize   ? connection.header[at]
                        : connection.page ? pgm_read_byte(MOBILE_PAGE + at - headerSize)
                                          : connection.body[at - headerSize];
        }
        const size_t wrote = connection.client.write(buffer, n);
        connection.sent += wrote;
        if (wrote)
            connection.progress = now;
    }
};
