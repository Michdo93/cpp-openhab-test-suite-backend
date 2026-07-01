/**
 * cpp-openhab-test-suite-backend
 * ───────────────────────────────
 * Stateless Crow HTTP server that proxies test-suite calls
 * from the GitHub Pages frontend to openHAB.
 *
 * Endpoints
 * ─────────
 *   GET  /             → health / wake-up
 *   POST /api/connect  → verify credentials  → { loggedIn, isCloud }
 *   POST /api/test     → run tester method   → { result, output }
 */

#include "crow.h"
#include <openhab/openhab.h>
#include <openhab/testsuite.h>
#include <nlohmann/json.hpp>

#include <sstream>
#include <iostream>
#include <string>
#include <functional>
#include <stdexcept>
#include <cstdlib>

using json = nlohmann::json;
using namespace openhab;
using namespace openhab::testsuite;

// ─── CORS middleware ──────────────────────────────────────────────────────────

struct CORSMiddleware {
    struct context {};
    void before_handle(crow::request&, crow::response& res, context&) { addCors(res); }
    void after_handle (crow::request&, crow::response& res, context&) { addCors(res); }
    static void addCors(crow::response& res) {
        res.set_header("Access-Control-Allow-Origin",  "*");
        res.set_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
        res.set_header("Access-Control-Allow-Headers", "Content-Type, Authorization");
    }
};

// ─── Response helpers ─────────────────────────────────────────────────────────

static crow::response ok(const json& j) {
    crow::response r(j.dump());
    r.set_header("Content-Type", "application/json");
    return r;
}
static crow::response err(const std::string& msg, int code = 400) {
    crow::response r(code, json{{"error", msg}}.dump());
    r.set_header("Content-Type", "application/json");
    return r;
}

// ─── Client factory ───────────────────────────────────────────────────────────

static OpenHABClient makeClient(const json& body) {
    std::string url      = body.value("url",      "");
    std::string username = body.value("username", "");
    std::string password = body.value("password", "");
    std::string token    = body.value("token",    "");
    if (url.empty()) throw std::invalid_argument("url is required");
    while (!url.empty() && url.back() == '/') url.pop_back();
    return OpenHABClient(url, username, password, token);
}

// ─── Output capture ───────────────────────────────────────────────────────────
/**
 * Redirect std::cout and std::cerr into a string buffer
 * for the duration of the tester call so diagnostic messages
 * are returned as "output" in the response.
 */
struct OutputCapture {
    std::ostringstream buf;
    std::streambuf*    oldCout;
    std::streambuf*    oldCerr;

    OutputCapture() {
        oldCout = std::cout.rdbuf(buf.rdbuf());
        oldCerr = std::cerr.rdbuf(buf.rdbuf());
    }
    ~OutputCapture() {
        std::cout.rdbuf(oldCout);
        std::cerr.rdbuf(oldCerr);
    }
    std::string str() const { return buf.str(); }
};

// ─── Tester dispatch ──────────────────────────────────────────────────────────
/**
 * Run a single tester method given the parsed request body.
 * Returns { result, output }.
 *
 * All tester classes share the constructor signature Tester(OpenHABClient&),
 * so we instantiate each one and call the method by name with the supplied params.
 *
 * Parameters are passed as a JSON array; each element is converted to the
 * appropriate C++ type by position (string, bool, int).
 */
static json dispatch(OpenHABClient& client,
                     const std::string& testerName,
                     const std::string& methodName,
                     const json& params) {

    // Helper to safely extract a string param
    auto str = [&](int i, const std::string& def = "") -> std::string {
        if (i < (int)params.size() && !params[i].is_null())
            return params[i].is_string() ? params[i].get<std::string>()
                                         : params[i].dump();
        return def;
    };
    auto boolP = [&](int i, bool def = false) -> bool {
        if (i < (int)params.size()) {
            if (params[i].is_boolean()) return params[i].get<bool>();
            auto s = params[i].is_string() ? params[i].get<std::string>() : params[i].dump();
            return s == "true" || s == "1";
        }
        return def;
    };
    auto intP = [&](int i, int def = 10) -> int {
        if (i < (int)params.size()) {
            if (params[i].is_number()) return params[i].get<int>();
            try { return std::stoi(params[i].get<std::string>()); } catch (...) {}
        }
        return def;
    };

    json result = false;
    std::string output;

    auto run = [&](auto callable) {
        OutputCapture cap;
        result = callable();
        output = cap.str();
    };

    // ── ItemTester ────────────────────────────────────────────────────────────
    if (testerName == "ItemTester") {
        ItemTester t(client);
        if      (methodName == "doesItemExist")
            run([&]{ return t.doesItemExist(str(0)); });
        else if (methodName == "checkItemIsType")
            run([&]{ return t.checkItemIsType(str(0), str(1)); });
        else if (methodName == "checkItemHasState")
            run([&]{ return t.checkItemHasState(str(0), str(1)); });
        else if (methodName == "isGroupItem")
            run([&]{ return t.isGroupItem(str(0)); });
        else if (methodName == "doesGroupContainMember")
            run([&]{ return t.doesGroupContainMember(str(0), str(1)); });
        else if (methodName == "checkGroupMemberState")
            run([&]{ return t.checkGroupMemberState(str(0), str(1), str(2)); });
        else if (methodName == "testSwitch")
            run([&]{ return t.testSwitch(str(0), str(1), str(2), intP(3)); });
        else if (methodName == "testContact")
            run([&]{ return t.testContact(str(0), str(1), str(2), intP(3)); });
        else if (methodName == "testColor")
            run([&]{ return t.testColor(str(0), str(1), str(2), intP(3)); });
        else if (methodName == "testDimmer")
            run([&]{ return t.testDimmer(str(0), str(1), str(2), intP(3)); });
        else if (methodName == "testRollershutter")
            run([&]{ return t.testRollershutter(str(0), str(1), str(2), intP(3)); });
        else if (methodName == "testNumber")
            run([&]{ return t.testNumber(str(0), str(1), str(2), intP(3)); });
        else if (methodName == "testPlayer")
            run([&]{ return t.testPlayer(str(0), str(1), str(2), intP(3)); });
        else if (methodName == "testDateTime")
            run([&]{ return t.testDateTime(str(0), str(1), str(2), intP(3)); });
        else if (methodName == "testLocation")
            run([&]{ return t.testLocation(str(0), str(1), str(2), intP(3)); });
        else if (methodName == "testImage")
            run([&]{ return t.testImage(str(0), str(1), str(2), intP(3)); });
        else if (methodName == "testString")
            run([&]{ return t.testString(str(0), str(1), str(2), intP(3)); });
        else if (methodName == "getGroupMembers") {
            OutputCapture cap;
            result = t.getGroupMembers(str(0));
            output = cap.str();
        }
        else throw std::invalid_argument("Unknown ItemTester method: " + methodName);
    }
    // ── ThingTester ───────────────────────────────────────────────────────────
    else if (testerName == "ThingTester") {
        ThingTester t(client);
        if      (methodName == "getThingStatus") {
            OutputCapture cap;
            result = t.getThingStatus(str(0));
            output = cap.str();
        }
        else if (methodName == "isThingStatus")
            run([&]{ return t.isThingStatus(str(0), str(1)); });
        else if (methodName == "isThingOnline")
            run([&]{ return t.isThingOnline(str(0)); });
        else if (methodName == "isThingOffline")
            run([&]{ return t.isThingOffline(str(0)); });
        else if (methodName == "isThingPending")
            run([&]{ return t.isThingPending(str(0)); });
        else if (methodName == "isThingUnknown")
            run([&]{ return t.isThingUnknown(str(0)); });
        else if (methodName == "isThingUninitialized")
            run([&]{ return t.isThingUninitialized(str(0)); });
        else if (methodName == "isThingError")
            run([&]{ return t.isThingError(str(0)); });
        else if (methodName == "enableThing")
            run([&]{ return t.enableThing(str(0)); });
        else if (methodName == "disableThing")
            run([&]{ return t.disableThing(str(0)); });
        else throw std::invalid_argument("Unknown ThingTester method: " + methodName);
    }
    // ── RuleTester ────────────────────────────────────────────────────────────
    else if (testerName == "RuleTester") {
        RuleTester t(client);
        if      (methodName == "getRuleStatus") {
            OutputCapture cap;
            result = t.getRuleStatus(str(0));
            output = cap.str();
        }
        else if (methodName == "isRuleActive")
            run([&]{ return t.isRuleActive(str(0)); });
        else if (methodName == "isRuleDisabled")
            run([&]{ return t.isRuleDisabled(str(0)); });
        else if (methodName == "isRuleRunning")
            run([&]{ return t.isRuleRunning(str(0)); });
        else if (methodName == "isRuleIdle")
            run([&]{ return t.isRuleIdle(str(0)); });
        else if (methodName == "enableRule")
            run([&]{ return t.enableRule(str(0)); });
        else if (methodName == "disableRule")
            run([&]{ return t.disableRule(str(0)); });
        else if (methodName == "runRule")
            run([&]{ return t.runRule(str(0)); });
        else if (methodName == "testRuleExecution")
            run([&]{ return t.testRuleExecution(str(0), str(1), str(2)); });
        else throw std::invalid_argument("Unknown RuleTester method: " + methodName);
    }
    // ── ChannelTester ─────────────────────────────────────────────────────────
    else if (testerName == "ChannelTester") {
        ChannelTester t(client);
        if      (methodName == "isItemLinkedToChannel")
            run([&]{ return t.isItemLinkedToChannel(str(0), str(1)); });
        else if (methodName == "getLinksForItem") {
            OutputCapture cap;
            result = t.getLinksForItem(str(0));
            output = cap.str();
        }
        else if (methodName == "isItemLinkedToAnyChannel")
            run([&]{ return t.isItemLinkedToAnyChannel(str(0)); });
        else if (methodName == "hasOrphanedLinks")
            run([&]{ return t.hasOrphanedLinks(); });
        else throw std::invalid_argument("Unknown ChannelTester method: " + methodName);
    }
    // ── PersistenceTester ─────────────────────────────────────────────────────
    else if (testerName == "PersistenceTester") {
        PersistenceTester t(client);
        if      (methodName == "isItemPersisted")
            run([&]{ return t.isItemPersisted(str(0), str(1)); });
        else if (methodName == "hasDataInRange")
            run([&]{ return t.hasDataInRange(str(0), str(1), str(2), str(3)); });
        else if (methodName == "checkLastPersistedState")
            run([&]{ return t.checkLastPersistedState(str(0), str(1), str(2)); });
        else throw std::invalid_argument("Unknown PersistenceTester method: " + methodName);
    }
    // ── SitemapTester ─────────────────────────────────────────────────────────
    else if (testerName == "SitemapTester") {
        SitemapTester t(client);
        if      (methodName == "doesSitemapExist")
            run([&]{ return t.doesSitemapExist(str(0)); });
        else if (methodName == "doesSitemapContainItem")
            run([&]{ return t.doesSitemapContainItem(str(0), str(1)); });
        else throw std::invalid_argument("Unknown SitemapTester method: " + methodName);
    }
    else {
        throw std::invalid_argument(
            "Unknown tester '" + testerName + "'. Valid: ItemTester, ThingTester, "
            "RuleTester, ChannelTester, PersistenceTester, SitemapTester");
    }

    // Trim trailing newlines from output
    while (!output.empty() && (output.back() == '\n' || output.back() == '\r'))
        output.pop_back();

    return json{{"result", result}, {"output", output}};
}

// ─── Main ─────────────────────────────────────────────────────────────────────

int main() {
    const char* portEnv = std::getenv("PORT");
    uint16_t port = portEnv ? static_cast<uint16_t>(std::stoi(portEnv)) : 8080;

    crow::App<CORSMiddleware> app;
    app.loglevel(crow::LogLevel::Warning);

    // ── CORS preflight — explicit routes for every endpoint ──────────────────
    // Crow's wildcard route /<path> misses the root "/" and can be unreliable
    // for preflight OPTIONS across versions. Explicit routes are more robust.
    auto corsHandler = [](const crow::request&, crow::response& res, const std::string&) {
        CORSMiddleware::addCors(res);
        res.code = 204;
        res.end();
    };
    CROW_ROUTE(app, "/")                .methods(crow::HTTPMethod::OPTIONS)([](){
        crow::response res(204); CORSMiddleware::addCors(res); return res; });
    CROW_ROUTE(app, "/api/connect")     .methods(crow::HTTPMethod::OPTIONS)([](){
        crow::response res(204); CORSMiddleware::addCors(res); return res; });
    CROW_ROUTE(app, "/api/test")        .methods(crow::HTTPMethod::OPTIONS)([](){
        crow::response res(204); CORSMiddleware::addCors(res); return res; });
    CROW_ROUTE(app, "/<path>")          .methods(crow::HTTPMethod::OPTIONS)(corsHandler);

    // Health check / wake-up
    CROW_ROUTE(app, "/").methods(crow::HTTPMethod::GET)
    ([]() -> crow::response {
        return ok({{"status","ok"},{"service","cpp-openhab-test-suite-backend"}});
    });

    // POST /api/connect
    CROW_ROUTE(app, "/api/connect").methods(crow::HTTPMethod::POST)
    ([](const crow::request& req) -> crow::response {
        try {
            auto body = json::parse(req.body);
            auto c    = makeClient(body);
            return ok({{"loggedIn", c.isLoggedIn()},
                       {"isCloud",  c.isCloud()}});
        } catch (const std::exception& e) {
            return ok({{"loggedIn",false},{"isCloud",false},{"error",e.what()}});
        }
    });

    // POST /api/test
    CROW_ROUTE(app, "/api/test").methods(crow::HTTPMethod::POST)
    ([](const crow::request& req) -> crow::response {
        json body;
        try { body = json::parse(req.body); }
        catch (...) { return err("Invalid JSON body"); }

        std::string testerName = body.value("tester","");
        std::string methodName = body.value("method","");
        json params            = body.value("params", json::array());

        if (testerName.empty()) return err("tester is required");
        if (methodName.empty()) return err("method is required");

        // Build client — direct initialisation via move constructor.
        // OpenHABClient's copy-assignment and copy-constructor are both deleted.
        // makeClient() returns by value; the move constructor transports it here.
        OpenHABClient client = [&]() -> OpenHABClient {
            try { return makeClient(body); }
            catch (const std::exception& e) {
                throw std::runtime_error(
                    std::string("Connection config error: ") + e.what());
            }
        }();
        if (!client.isLoggedIn())
            return err("Could not connect to openHAB — check credentials", 401);


        // Dispatch
        try {
            auto result = dispatch(client, testerName, methodName, params);
            return ok(result);
        } catch (const std::invalid_argument& e) {
            return err(e.what(), 400);
        } catch (const OpenHABException& e) {
            return err(std::string("openHAB error: ") + e.what(), 502);
        } catch (const std::exception& e) {
            return err(e.what(), 500);
        }
    });

    std::cout << "cpp-openhab-test-suite-backend running on port " << port << "\n";
    app.port(port).multithreaded().run();
    return 0;
}