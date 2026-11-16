// Backend of the cpp-openhab-test-suite test app: runs the testers of the C++ library on
// behalf of the browser.
//
//   GET  /             health check (wake-up on Render)
//   POST /api/connect  {url, token | username+password}            -> {loggedIn, isCloud, url}
//   POST /api/test     {url, token, ..., tester, method, params: [...]} -> {result, output}
//
// params are positional as in the Python suite. The test… methods take
// [itemName, command, expectedState, timeout]; expectedState may also be a list of
// accepted states.
#include "httplib.h"

#include <openhab/testsuite.hpp>

#include <cstdlib>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

using openhab::json;
namespace ts = openhab::testsuite;

namespace {

// ── Messages of the testers: collected per request ───────────────────────────
// setLog() is global, so tests run one after another.
std::mutex testMutex;
std::string testOutput;

void setCORS(httplib::Response& res) {
    res.set_header("Access-Control-Allow-Origin", "*");
    res.set_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    res.set_header("Access-Control-Allow-Headers", "Content-Type");
}

void sendJson(httplib::Response& res, const json& value, int status = 200) {
    setCORS(res);
    res.status = status;
    res.set_content(value.dump(), "application/json");
}

void sendError(httplib::Response& res, const std::string& msg, int status) {
    sendJson(res, json{{"error", msg}}, status);
}

std::optional<std::string> opt(const json& body, const char* key) {
    std::string s = openhab::str(body, key);
    return s.empty() ? std::nullopt : std::optional<std::string>(s);
}

std::unique_ptr<openhab::OpenHABClient> makeClient(const json& body) {
    openhab::ClientOptions o;
    o.token = opt(body, "token");
    o.username = opt(body, "username");
    o.password = opt(body, "password");
    std::string url = openhab::str(body, "url");
    return std::make_unique<openhab::OpenHABClient>(url.empty() ? "https://myopenhab.org" : url, o);
}

// ── Parameters ────────────────────────────────────────────────────────────────
struct Params {
    json list;

    const json& at(size_t i) const {
        static const json none = nullptr;
        return i < list.size() ? list[i] : none;
    }
    std::string s(size_t i) const {
        const json& v = at(i);
        if (v.is_string()) return v.get<std::string>();
        if (v.is_number()) return v.dump();
        return "";
    }
    double num(size_t i, double def) const {
        const json& v = at(i);
        if (v.is_number()) return v.get<double>();
        if (v.is_string() && !v.get<std::string>().empty()) return std::atof(v.get<std::string>().c_str());
        return def;
    }
    // expectedState: one state, a list of accepted states or nothing
    ts::ExpectedState expected(size_t i) const {
        const json& v = at(i);
        if (v.is_array()) {
            std::vector<std::string> states;
            for (const json& e : v) states.push_back(e.is_string() ? e.get<std::string>() : e.dump());
            return ts::ExpectedState(states);
        }
        std::string t = s(i);
        return t.empty() ? ts::ExpectedState() : ts::ExpectedState(t);
    }
};

using Fn = std::function<json(openhab::OpenHABClient&, const Params&)>;

#define I(expr) [](openhab::OpenHABClient& c, const Params& p) -> json { ts::ItemTester t(c); (void)p; return expr; }
#define TH(expr) [](openhab::OpenHABClient& c, const Params& p) -> json { ts::ThingTester t(c); (void)p; return expr; }
#define R(expr) [](openhab::OpenHABClient& c, const Params& p) -> json { ts::RuleTester t(c); (void)p; return expr; }
#define CH(expr) [](openhab::OpenHABClient& c, const Params& p) -> json { ts::ChannelTester t(c); (void)p; return expr; }
#define PE(expr) [](openhab::OpenHABClient& c, const Params& p) -> json { ts::PersistenceTester t(c); (void)p; return expr; }
#define SI(expr) [](openhab::OpenHABClient& c, const Params& p) -> json { ts::SitemapTester t(c); (void)p; return expr; }
#define TEST(name) I(t.name(p.s(0), p.s(1), p.expected(2), static_cast<int>(p.num(3, 10))))

const std::map<std::string, std::map<std::string, Fn>> TESTERS = {
    {"ItemTester", {
        {"doesItemExist", I(t.doesItemExist(p.s(0)))},
        {"checkItemIsType", I(t.checkItemIsType(p.s(0), p.s(1)))},
        {"checkItemHasState", I(t.checkItemHasState(p.s(0), p.s(1)))},
        {"isGroupItem", I(t.isGroupItem(p.s(0)))},
        {"getGroupMembers", I(t.getGroupMembers(p.s(0)))},
        {"doesGroupContainMember", I(t.doesGroupContainMember(p.s(0), p.s(1)))},
        {"checkGroupMemberState", I(t.checkGroupMemberState(p.s(0), p.s(1), p.s(2)))},
        {"testColor", TEST(testColor)},
        {"testContact", I(t.testContact(p.s(0), p.at(1).is_null() ? std::nullopt
                                                                  : std::optional<std::string>(p.s(1)),
                                         p.expected(2), static_cast<int>(p.num(3, 10))))},
        {"testDateTime", TEST(testDateTime)},
        {"testDimmer", TEST(testDimmer)},
        {"testImage", TEST(testImage)},
        {"testLocation", TEST(testLocation)},
        {"testNumber", TEST(testNumber)},
        {"testPlayer", TEST(testPlayer)},
        {"testRollershutter", TEST(testRollershutter)},
        {"testString", TEST(testString)},
        {"testSwitch", TEST(testSwitch)},
    }},
    {"ThingTester", {
        {"getThingStatus", TH(t.getThingStatus(p.s(0)))},
        {"isThingStatus", TH(t.isThingStatus(p.s(0), p.s(1)))},
        {"isThingOnline", TH(t.isThingOnline(p.s(0)))},
        {"isThingOffline", TH(t.isThingOffline(p.s(0)))},
        {"isThingPending", TH(t.isThingPending(p.s(0)))},
        {"isThingUnknown", TH(t.isThingUnknown(p.s(0)))},
        {"isThingUninitialized", TH(t.isThingUninitialized(p.s(0)))},
        {"isThingError", TH(t.isThingError(p.s(0)))},
        {"enableThing", TH(t.enableThing(p.s(0)))},
        {"disableThing", TH(t.disableThing(p.s(0)))},
    }},
    {"RuleTester", {
        {"runRule", R(t.runRule(p.s(0), p.at(1).is_object() ? p.at(1) : json()))},
        {"enableRule", R(t.enableRule(p.s(0)))},
        {"disableRule", R(t.disableRule(p.s(0)))},
        {"testRuleExecution", R(t.testRuleExecution(p.s(0), p.s(1), p.s(2), p.num(3, 2.0)))},
        {"isRuleActive", R(t.isRuleActive(p.s(0)))},
        {"isRuleDisabled", R(t.isRuleDisabled(p.s(0)))},
        {"isRuleRunning", R(t.isRuleRunning(p.s(0)))},
        {"isRuleIdle", R(t.isRuleIdle(p.s(0)))},
        {"getRuleStatus", R(t.getRuleStatus(p.s(0)))},
    }},
    {"ChannelTester", {
        {"isItemLinkedToChannel", CH(t.isItemLinkedToChannel(p.s(0), p.s(1)))},
        {"getLinksForItem", CH(t.getLinksForItem(p.s(0)))},
        {"hasOrphanedLinks", CH(t.hasOrphanedLinks())},
        {"isItemLinkedToAnyChannel", CH(t.isItemLinkedToAnyChannel(p.s(0)))},
    }},
    {"PersistenceTester", {
        {"isItemPersisted", PE(t.isItemPersisted(p.s(0), p.s(1)))},
        {"hasDataInRange", PE(t.hasDataInRange(p.s(0), p.s(1), p.s(2), p.s(3)))},
        {"checkLastPersistedState", PE(t.checkLastPersistedState(p.s(0), p.s(1), p.s(2)))},
    }},
    {"SitemapTester", {
        {"doesSitemapExist", SI(t.doesSitemapExist(p.s(0)))},
        {"doesSitemapContainItem", SI(t.doesSitemapContainItem(p.s(0), p.s(1)))},
    }},
};

}  // namespace

int main() {
    ts::setLog([](int level, const std::string& message) {
        if (level >= 1) testOutput += "ERROR: ";
        testOutput += message + "\n";
    });
    httplib::Server svr;

    svr.Options(".*", [](const httplib::Request&, httplib::Response& res) {
        setCORS(res);
        res.status = 204;
    });

    svr.Get("/", [](const httplib::Request&, httplib::Response& res) {
        sendJson(res, json{{"status", "ok"}, {"service", "cpp-openhab-test-suite-backend"},
                           {"version", ts::VERSION}});
    });

    svr.Post("/api/connect", [](const httplib::Request& req, httplib::Response& res) {
        json body = json::parse(req.body, nullptr, false);
        if (!body.is_object()) return sendError(res, "Invalid JSON body", 400);
        auto c = makeClient(body);
        json j{{"loggedIn", c->isLoggedIn()}, {"isCloud", c->isCloud()}, {"url", c->url()}};
        if (!c->lastError().empty()) j["error"] = c->lastError();
        sendJson(res, j);
    });

    svr.Post("/api/test", [](const httplib::Request& req, httplib::Response& res) {
        json body = json::parse(req.body, nullptr, false);
        if (!body.is_object()) return sendError(res, "Invalid JSON body", 400);
        std::string tester = openhab::str(body, "tester"), method = openhab::str(body, "method");
        if (tester.empty() || method.empty())
            return sendError(res, "tester and method are required", 400);
        auto t = TESTERS.find(tester);
        if (t == TESTERS.end())
            return sendError(res, "Unknown tester '" + tester + "'. Valid: ItemTester, "
                             "ThingTester, RuleTester, ChannelTester, PersistenceTester, "
                             "SitemapTester", 400);
        auto m = t->second.find(method);
        if (m == t->second.end())
            return sendError(res, "Unknown " + tester + " method: " + method, 400);

        auto c = makeClient(body);
        if (!c->isLoggedIn())
            return sendError(res, "Could not connect to openHAB - check URL and credentials", 401);
        Params p{body.contains("params") && body["params"].is_array() ? body["params"]
                                                                       : json::array()};
        std::lock_guard<std::mutex> lock(testMutex);
        testOutput.clear();
        json result = m->second(*c, p);
        while (!testOutput.empty() && (testOutput.back() == '\n' || testOutput.back() == ' '))
            testOutput.pop_back();
        sendJson(res, json{{"result", result}, {"output", testOutput}});
    });

    const char* portEnv = std::getenv("PORT");
    int port = portEnv ? std::atoi(portEnv) : 8080;
    svr.set_read_timeout(120);
    svr.set_write_timeout(120);
    std::cout << "cpp-openhab-test-suite " << ts::VERSION << " backend listening on port " << port
              << std::endl;
    svr.listen("0.0.0.0", port);
    return 0;
}
