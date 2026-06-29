# cpp-openhab-test-suite-backend

Stateless C++ (Crow) HTTP backend for the
[cpp-openhab-test-suite](https://github.com/Michdo93/cpp-openhab-test-suite)
web frontend.

Every request carries credentials in the body — no session state is stored.
`std::cout` and `std::cerr` are redirected into a string buffer during each
tester call so diagnostic messages are returned as `"output"` in the response.

## Endpoints

| Method | Path | Description |
|---|---|---|
| `GET` | `/` | Health check / wake-up |
| `POST` | `/api/connect` | Verify credentials → `{ loggedIn, isCloud }` |
| `POST` | `/api/test` | Run a tester method → `{ result, output }` |

### `POST /api/test`

```json
{
  "url":      "https://myopenhab.org",
  "username": "user@example.com",
  "password": "secret",
  "tester":   "ItemTester",
  "method":   "testSwitch",
  "params":   ["MySwitch", "ON", "ON", 10]
}
```

Available testers: `ItemTester`, `ThingTester`, `RuleTester`,
`ChannelTester`, `PersistenceTester`, `SitemapTester`.

## Git submodules

The backend pulls the REST client and the test suite as submodules:

```
extern/
├── cpp-openhab-rest-client/   ← https://github.com/Michdo93/cpp-openhab-rest-client
└── cpp-openhab-test-suite/    ← https://github.com/Michdo93/cpp-openhab-test-suite
```

Clone with submodules:

```bash
git clone --recurse-submodules \
  https://github.com/Michdo93/cpp-openhab-test-suite-backend.git
```

## Local build

```bash
sudo apt-get install -y libcurl4-openssl-dev cmake build-essential
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -- -j$(nproc)
PORT=8080 ./openhab_test_suite_backend
```

## Docker

```bash
docker build -t cpp-openhab-test-suite-backend .
docker run -p 8080:8080 cpp-openhab-test-suite-backend
```

## Deploy on Render.com

1. Push `cpp-openhab-rest-client` and `cpp-openhab-test-suite` to GitHub.
2. Push this repository to GitHub **with initialised submodules**:
   ```bash
   git submodule add https://github.com/Michdo93/cpp-openhab-rest-client.git \
       extern/cpp-openhab-rest-client
   git submodule add https://github.com/Michdo93/cpp-openhab-test-suite.git \
       extern/cpp-openhab-test-suite
   git add . && git commit -m "add submodules"
   git push origin main
   ```
3. On [render.com](https://render.com): **New → Web Service → Docker →
   Frankfurt → Free → PORT=8080 → Deploy**.

Live URL: `https://cpp-openhab-test-suite-backend.onrender.com`

## License

MIT
