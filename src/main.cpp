/**
 * cpp-openhab-test-suite-backend
 * ───────────────────────────────
 * HTTP backend using cpp-httplib (header-only, no Crow/asio dependency).
 * Replaces the previous Crow-based implementation which had unreliable
 * CORS handling on OPTIONS preflight requests.
 *
 * Endpoints
 *   GET  /             → health / wake-up
 *   POST /api/connect  → verify credentials → { loggedIn, isCloud }
 *   POST /api/test     → run tester method  → { result, output }
 */

#define CPPHTTPLIB_OPENSSL_SUPPORT 0
#include "httplib.h"

#include <openhab/openhab.h>
#include <openhab/testsuite.h>
#include <nlohmann/json.hpp>

#include <sstream>
#include <iostream>
#include <string>
#include <stdexcept>
#include <cstdlib>
#include <unistd.h>

using json = nlohmann::json;
using namespace openhab;
using namespace openhab::testsuite;

// ─── CORS ─────────────────────────────────────────────────────────────────────

static void setCORS(httplib::Response& res) {
    res.set_header("Access-Control-Allow-Origin",  "*");
    res.set_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    res.set_header("Access-Control-Allow-Headers", "Content-Type, Authorization");
}

// ─── Response helpers ─────────────────────────────────────────────────────────

static void respond_ok(httplib::Response& res, const json& j) {
    setCORS(res);
    res.set_content(j.dump(), "application/json");
}

static void respond_err(httplib::Response& res, const std::string& msg, int code = 400) {
    setCORS(res);
    res.status = code;
    res.set_content(json{{"error", msg}}.dump(), "application/json");
}

// ─── Client factory ───────────────────────────────────────────────────────────

static std::string jsonStr(const json& body, const std::string& key) {
    // body.value(key, "") throws type_error.302 when the field exists but is
    // JSON null (frontend sends null for empty optional fields). Handle both
    // "absent" and "null" gracefully by returning an empty string.
    if (!body.contains(key) || body[key].is_null()) return "";
    return body[key].get<std::string>();
}

static OpenHABClient makeClient(const json& body) {
    std::string url  = jsonStr(body, "url");
    std::string user = jsonStr(body, "username");
    std::string pass = jsonStr(body, "password");
    std::string tok  = jsonStr(body, "token");
    if (url.empty()) throw std::invalid_argument("url is required");
    while (!url.empty() && url.back() == '/') url.pop_back();
    // Normalize: add http:// if no protocol given
    if (url.rfind("http://", 0) != 0 && url.rfind("https://", 0) != 0)
        url = "http://" + url;
    return OpenHABClient(url, user, pass, tok);
}

// ─── Output capture ───────────────────────────────────────────────────────────

struct Capture {
    std::ostringstream buf;
    std::streambuf*    oldOut;
    std::streambuf*    oldErr;
    Capture() {
        oldOut = std::cout.rdbuf(buf.rdbuf());
        oldErr = std::cerr.rdbuf(buf.rdbuf());
    }
    ~Capture() {
        std::cout.rdbuf(oldOut);
        std::cerr.rdbuf(oldErr);
    }
    std::string str() const { return buf.str(); }
};

// ─── Parameter helpers ────────────────────────────────────────────────────────

static std::string sp(const json& p, int i, const std::string& def = "") {
    if (i < (int)p.size() && !p[i].is_null())
        return p[i].is_string() ? p[i].get<std::string>() : p[i].dump();
    return def;
}
static int ip(const json& p, int i, int def = 10) {
    if (i < (int)p.size()) {
        if (p[i].is_number()) return p[i].get<int>();
        try { return std::stoi(sp(p, i)); } catch (...) {}
    }
    return def;
}

// ─── Dispatch ─────────────────────────────────────────────────────────────────

static json dispatch(OpenHABClient& c,
                     const std::string& tester,
                     const std::string& method,
                     const json& p) {
    json result = false;
    std::string output;

    auto run_bool = [&](auto fn) {
        Capture cap;
        result = (fn() != 0);
        output = cap.str();
    };
    auto run_str = [&](auto fn) {
        Capture cap;
        auto r  = fn();
        result  = r.dump();
        output  = cap.str();
    };

    if (tester == "ItemTester") {
        ItemTester t(c);
        if      (method == "doesItemExist")
            run_bool([&]{ return t.doesItemExist(sp(p,0)); });
        else if (method == "checkItemIsType")
            run_bool([&]{ return t.checkItemIsType(sp(p,0), sp(p,1)); });
        else if (method == "checkItemHasState")
            run_bool([&]{ return t.checkItemHasState(sp(p,0), sp(p,1)); });
        else if (method == "isGroupItem")
            run_bool([&]{ return t.isGroupItem(sp(p,0)); });
        else if (method == "doesGroupContainMember")
            run_bool([&]{ return t.doesGroupContainMember(sp(p,0), sp(p,1)); });
        else if (method == "checkGroupMemberState")
            run_bool([&]{ return t.checkGroupMemberState(sp(p,0), sp(p,1), sp(p,2)); });
        else if (method == "getGroupMembers") {
            Capture cap;
            result = t.getGroupMembers(sp(p,0));
            output = cap.str();
        }
        else if (method == "testSwitch")
            run_bool([&]{ return t.testSwitch(sp(p,0), sp(p,1), sp(p,2), ip(p,3)); });
        else if (method == "testContact")
            run_bool([&]{ return t.testContact(sp(p,0), sp(p,1), sp(p,2), ip(p,3)); });
        else if (method == "testColor")
            run_bool([&]{ return t.testColor(sp(p,0), sp(p,1), sp(p,2), ip(p,3)); });
        else if (method == "testDimmer")
            run_bool([&]{ return t.testDimmer(sp(p,0), sp(p,1), sp(p,2), ip(p,3)); });
        else if (method == "testRollershutter")
            run_bool([&]{ return t.testRollershutter(sp(p,0), sp(p,1), sp(p,2), ip(p,3)); });
        else if (method == "testNumber")
            run_bool([&]{ return t.testNumber(sp(p,0), sp(p,1), sp(p,2), ip(p,3)); });
        else if (method == "testPlayer")
            run_bool([&]{ return t.testPlayer(sp(p,0), sp(p,1), sp(p,2), ip(p,3)); });
        else if (method == "testDateTime")
            run_bool([&]{ return t.testDateTime(sp(p,0), sp(p,1), sp(p,2), ip(p,3)); });
        else if (method == "testLocation")
            run_bool([&]{ return t.testLocation(sp(p,0), sp(p,1), sp(p,2), ip(p,3)); });
        else if (method == "testImage")
            run_bool([&]{ return t.testImage(sp(p,0), sp(p,1), sp(p,2), ip(p,3)); });
        else if (method == "testString")
            run_bool([&]{ return t.testString(sp(p,0), sp(p,1), sp(p,2), ip(p,3)); });
        else throw std::invalid_argument("Unknown ItemTester method: " + method);
    }
    else if (tester == "ThingTester") {
        ThingTester t(c);
        if      (method == "getThingStatus") {
            Capture cap;
            result = t.getThingStatus(sp(p,0));
            output = cap.str();
        }
        else if (method == "isThingStatus")
            run_bool([&]{ return t.isThingStatus(sp(p,0), sp(p,1)); });
        else if (method == "isThingOnline")       run_bool([&]{ return t.isThingOnline(sp(p,0)); });
        else if (method == "isThingOffline")      run_bool([&]{ return t.isThingOffline(sp(p,0)); });
        else if (method == "isThingPending")      run_bool([&]{ return t.isThingPending(sp(p,0)); });
        else if (method == "isThingUnknown")      run_bool([&]{ return t.isThingUnknown(sp(p,0)); });
        else if (method == "isThingUninitialized")run_bool([&]{ return t.isThingUninitialized(sp(p,0)); });
        else if (method == "isThingError")        run_bool([&]{ return t.isThingError(sp(p,0)); });
        else if (method == "enableThing")         run_bool([&]{ return t.enableThing(sp(p,0)); });
        else if (method == "disableThing")        run_bool([&]{ return t.disableThing(sp(p,0)); });
        else throw std::invalid_argument("Unknown ThingTester method: " + method);
    }
    else if (tester == "RuleTester") {
        RuleTester t(c);
        if      (method == "getRuleStatus") {
            Capture cap;
            result = t.getRuleStatus(sp(p,0));
            output = cap.str();
        }
        else if (method == "isRuleActive")   run_bool([&]{ return t.isRuleActive(sp(p,0)); });
        else if (method == "isRuleDisabled") run_bool([&]{ return t.isRuleDisabled(sp(p,0)); });
        else if (method == "isRuleRunning")  run_bool([&]{ return t.isRuleRunning(sp(p,0)); });
        else if (method == "isRuleIdle")     run_bool([&]{ return t.isRuleIdle(sp(p,0)); });
        else if (method == "enableRule")     run_bool([&]{ return t.enableRule(sp(p,0)); });
        else if (method == "disableRule")    run_bool([&]{ return t.disableRule(sp(p,0)); });
        else if (method == "runRule")        run_bool([&]{ return t.runRule(sp(p,0)); });
        else if (method == "testRuleExecution")
            run_bool([&]{ return t.testRuleExecution(sp(p,0), sp(p,1), sp(p,2)); });
        else throw std::invalid_argument("Unknown RuleTester method: " + method);
    }
    else if (tester == "ChannelTester") {
        ChannelTester t(c);
        if      (method == "isItemLinkedToChannel")
            run_bool([&]{ return t.isItemLinkedToChannel(sp(p,0), sp(p,1)); });
        else if (method == "getLinksForItem") {
            Capture cap;
            result = t.getLinksForItem(sp(p,0));
            output = cap.str();
        }
        else if (method == "isItemLinkedToAnyChannel")
            run_bool([&]{ return t.isItemLinkedToAnyChannel(sp(p,0)); });
        else if (method == "hasOrphanedLinks")
            run_bool([&]{ return t.hasOrphanedLinks(); });
        else throw std::invalid_argument("Unknown ChannelTester method: " + method);
    }
    else if (tester == "PersistenceTester") {
        PersistenceTester t(c);
        if      (method == "isItemPersisted")
            run_bool([&]{ return t.isItemPersisted(sp(p,0), sp(p,1)); });
        else if (method == "hasDataInRange")
            run_bool([&]{ return t.hasDataInRange(sp(p,0), sp(p,1), sp(p,2), sp(p,3)); });
        else if (method == "checkLastPersistedState")
            run_bool([&]{ return t.checkLastPersistedState(sp(p,0), sp(p,1), sp(p,2)); });
        else throw std::invalid_argument("Unknown PersistenceTester method: " + method);
    }
    else if (tester == "SitemapTester") {
        SitemapTester t(c);
        if      (method == "doesSitemapExist")
            run_bool([&]{ return t.doesSitemapExist(sp(p,0)); });
        else if (method == "doesSitemapContainItem")
            run_bool([&]{ return t.doesSitemapContainItem(sp(p,0), sp(p,1)); });
        else throw std::invalid_argument("Unknown SitemapTester method: " + method);
    }
    else {
        throw std::invalid_argument(
            "Unknown tester '" + tester + "'. Valid: ItemTester, ThingTester, "
            "RuleTester, ChannelTester, PersistenceTester, SitemapTester");
    }

    while (!output.empty() &&
           (output.back() == '\n' || output.back() == '\r' || output.back() == ' '))
        output.pop_back();

    return json{{"result", result}, {"output", output}};
}

// ─── Main ─────────────────────────────────────────────────────────────────────

int main() {
    const char* portEnv = std::getenv("PORT");
    int port = portEnv ? std::stoi(portEnv) : 8080;

    httplib::Server svr;

    // CORS preflight — regex ".*" matches every path reliably
    svr.Options(".*", [](const httplib::Request&, httplib::Response& res) {
        setCORS(res);
        res.status = 204;
    });

    // Health check
    svr.Get("/", [](const httplib::Request&, httplib::Response& res) {
        respond_ok(res, {{"status","ok"},
                         {"service","cpp-openhab-test-suite-backend"}});
    });

    // POST /api/connect
    svr.Post("/api/connect", [](const httplib::Request& req, httplib::Response& res) {
        try {
            auto body = json::parse(req.body);
            auto c    = makeClient(body);
            respond_ok(res, {{"loggedIn", c.isLoggedIn()},
                             {"isCloud",  c.isCloud()}});
        } catch (const std::exception& e) {
            respond_ok(res, {{"loggedIn",false},{"isCloud",false},{"error",e.what()}});
        }
    });

    // POST /api/test
    svr.Post("/api/test", [](const httplib::Request& req, httplib::Response& res) {
        json body;
        try { body = json::parse(req.body); }
        catch (...) { respond_err(res, "Invalid JSON body"); return; }

        std::string tester = body.value("tester", "");
        std::string method = body.value("method", "");
        json params        = body.value("params", json::array());

        if (tester.empty()) { respond_err(res, "tester is required"); return; }
        if (method.empty()) { respond_err(res, "method is required"); return; }

        // Build client via move constructor (copy-assignment/ctor are deleted)
        OpenHABClient client = [&]() -> OpenHABClient {
            try { return makeClient(body); }
            catch (const std::exception& e) {
                throw std::runtime_error(
                    std::string("Connection config error: ") + e.what());
            }
        }();

        if (!client.isLoggedIn()) {
            respond_err(res,
                "Could not connect to openHAB — check URL and credentials", 401);
            return;
        }

        try {
            auto result = dispatch(client, tester, method, params);
            respond_ok(res, result);
        } catch (const std::invalid_argument& e) {
            respond_err(res, e.what(), 400);
        } catch (const OpenHABException& e) {
            respond_err(res, std::string("openHAB error: ") + e.what(), 502);
        } catch (const std::exception& e) {
            respond_err(res, e.what(), 500);
        }
    });

    std::cout << "cpp-openhab-test-suite-backend running on port " << port << "\n";
    svr.listen("0.0.0.0", port);
    return 0;
}