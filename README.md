# cpp-openhab-test-suite-backend

HTTP backend for the [cpp-openhab-test-suite test app](https://github.com/Michdo93/cpp-openhab-test-suite-test-app):
a browser cannot run C++, so this small server runs the testers of
[cpp-openhab-test-suite](https://github.com/Michdo93/cpp-openhab-test-suite) 2.0.0 on its behalf.
All 48 tester methods are available.

| Method | Path | Body | Answer |
|---|---|---|---|
| `GET` | `/` | – | `{"status":"ok","service":"cpp-openhab-test-suite-backend","version":"2.0.0"}` |
| `POST` | `/api/connect` | `{url, token}` or `{url, username, password}` | `{loggedIn, isCloud, url}` |
| `POST` | `/api/test` | `{url, token, tester, method, params: [...]}` | `{result, output}` |

```json
{
  "url": "http://openhab:8080", "token": "oh.xxx",
  "tester": "ItemTester", "method": "testDimmer",
  "params": ["Wohnzimmer_Dimmer", "30", ["30", "30.0"], 5]
}
```

```json
{"result": true, "output": "Success: 'Wohnzimmer_Dimmer' reached state '30'."}
```

`params` are positional as in the Python suite. The `test…` methods take
`[itemName, command, expectedState, timeout]`; `expectedState` may be a list of accepted states
(`ExpectedState` of the C++ library). `result` is `true`/`false`, for `getGroupMembers`,
`getLinksForItem` and `getRuleStatus` JSON, for `getThingStatus` a string. `output` holds the
messages of the testers (`openhab::testsuite::setLog()`).

Testers: `ItemTester`, `ThingTester`, `RuleTester`, `ChannelTester`, `PersistenceTester`,
`SitemapTester`. The backend keeps no credentials; they come with every request. Tests run one
after another.

## Build and run

```bash
cmake -S . -B build && cmake --build build -j2
PORT=8080 ./build/openhab_test_suite_backend
```

Requirements: C++17 compiler, CMake 3.16+, libcurl, nlohmann/json; cpp-openhab-test-suite,
cpp-openhab-rest-client and cpp-httplib are fetched by CMake. For local library sources:
`-DOPENHAB_TEST_SUITE_DIR=../cpp-openhab-test-suite -DOPENHAB_REST_CLIENT_DIR=../cpp-openhab-rest-client`.

Docker: `docker build -t cpp-openhab-test-suite-backend .` – `render.yaml` deploys it on Render
(Frankfurt, free plan): `https://cpp-openhab-test-suite-backend.onrender.com`.

## License

MIT – see [LICENSE](LICENSE).
