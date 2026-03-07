#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "support/fake_ws_transport.hpp"
#include "trading/adapters/exchanges/kalshi/auth_signer.hpp"
#include "trading/adapters/exchanges/kalshi/ws_adapter.hpp"
#include "trading/adapters/ws/session.hpp"
#include "trading/engine/runtime.hpp"

namespace {

constexpr const char* kTestPrivateKeyPem = R"(-----BEGIN PRIVATE KEY-----
MIIEvAIBADANBgkqhkiG9w0BAQEFAASCBKYwggSiAgEAAoIBAQDC0wMNDKatICXc
VHJGfoGoMmz8BoF7HwhTl7CCXAZ55lLvCaLY2LhZumiWRuIHaHZDl2DhZMCDNJ3F
eIEawGjpjlWsj7WGXzSsTZUQLS0TVq5Jr5AtFWbvFn/cYySD47/aFB3mqgUPDS7G
0tTx/RpHjsYdp4j/aES5lBArQVzWVjd8zlKP90Q2J39H2soahvUUNW6hxhz2XFPq
N6YgKclwSApt9177ShO6fPQ7wa4RL+1rRbLPTkZKbW4S2JF8xZ1/RYUFZNKwG1WX
Hp4ZY8wIDReDIN9/ksh33DbVGloIjbJfRo8dMqawwIw03ZJol8KbQIGOlxlLEbdP
H4mgFv7ZAgMBAAECggEAA45NQpzq7lTFOH8/Qpz8tw/jpJFPAzDLMfPvqBkVexXG
5fU7RXdaP6kwCXMKV3z0ROgkVxmJpBE8E9xO5mL7sWav/3zocmFKsh5MebihujvK
iD/93SXhSPb9GHyBB65n3QtP/zfZPnhsyO8sClBg1HsdMqB/tAI7Y8tFGUeOmr9O
WOY359vG5xJlCBhhQ1hWaxVn1zb+Bs3Cs4dqpFnRpJP0xUuMKi6TDXHhUnWsw6Rr
cSC3zeLJ/2lDO58X9T5GEuIjvIZjkDNrvkV3OgLiHAt6R2b+scXirZHzYwAd1s3o
W1vmLPkAAXIip2IYndtIBjMEqMp5+6dHaXel83H0FQKBgQDqesT746NvBwDpSmZj
ho6XiyTQKS6SERK3I3hSKRAXgdOfc5OLWCaHpgitnhaCQ4Zl4lusnuBNFkJnLUZ3
v2CTjrGqUCZtOAy+5k2Fr+HwmmDGCqYXfN6xbZfH0y+kzk8lDZAyT/VkMv3gmtTE
A4FQKVb870xra5J5jwtxEerD1QKBgQDUtITDCtr1aWW6dUF8JA5S3u3HiC2XOdTZ
h5dzLxHWCPtIJrTSYINrx0kqvr6uefTOlWel5ckJ8l/qwPtEyZ36SaQvvrxhew5/
monY3kKLisBQLFyfdwAgKjOTBso3Ydfi0OL5GfnVgRdgPSt96OarLHf/Oj252Ef0
/B9AiFFE9QKBgELrZuy8uxgk1429PhuJe4iknY0LM89cMAs0hFJuhbkvxEXtpl5O
ejCcoj1qsOHcg67wO1m4vitB/xGTSllCtT1nrCz/Uvg41upksBtHZyRT6uqaf2yU
prncnUTacO6IMu5QQKcbSt2d7/b6OV734SAPrFPjt9uwh81JlepfQrFBAoGAYrD/
iJ/YyFWG5eTR5Y+1Na5KFXKR9MDYibXoB4GnfO/d57RN4e72C2QEBtlKEBp2BAQ+
ezMK/JqA9qNj8p65tg/FJoIRVgmKyJojq4Q0zBos8QYkU7kbTdFG7MkRunCjgpH5
PitXSEEFX5oCPAF8RZ/0bW/QhBJeEMWKmp6lVqECgYBQOlGbPcpEIDo738T3yADv
dKLBmz0Z/Rgg22dVhjhhUKYTzjc8spVyUFmXjm9/RFrqZRBZ+r52ZZS9mO/D50PR
Hs7GYP2lYjn+hqDzxRLUDyDFxdKcxVtz4ye9iTCiaOFdCBRDQOMiViyG3bzaEWqv
wMev4lw7N9lCKEbZCmutaA==
-----END PRIVATE KEY-----)";

trading::adapters::exchanges::kalshi::WsAdapter make_kalshi_adapter() {
    return trading::adapters::exchanges::kalshi::WsAdapter{
        trading::adapters::exchanges::kalshi::AuthSigner{
            trading::adapters::exchanges::kalshi::Credentials{
                .key_id = "test-key-id",
                .private_key_pem = kTestPrivateKeyPem,
            },
        },
    };
}

TEST(TradingCoreTest, StartupPayloadHasExpectedFields) {
    const auto startup = trading::engine::build_trader_startup_payload("test");
    const auto startup_json = nlohmann::json::parse(startup);

    EXPECT_EQ(startup_json.at("service"), "trading_engine");
    EXPECT_EQ(startup_json.at("status"), "ok");
    EXPECT_EQ(startup_json.at("mode"), "test");
}

TEST(WsSessionTest, ConnectBuildsKalshiHeaders) {
    const auto kalshi_adapter = make_kalshi_adapter();
    trading::tests::FakeWsTransport transport;
    trading::adapters::ws::WsSession session{transport, kalshi_adapter};

    ASSERT_TRUE(session.connect());
    const auto& connect_request = session.connect_request();

    EXPECT_FALSE(connect_request.endpoint.empty());
    EXPECT_TRUE(connect_request.headers.contains("KALSHI-ACCESS-KEY"));
    EXPECT_TRUE(connect_request.headers.contains("KALSHI-ACCESS-SIGNATURE"));
    EXPECT_TRUE(connect_request.headers.contains("KALSHI-ACCESS-TIMESTAMP"));
    EXPECT_FALSE(connect_request.headers.at("KALSHI-ACCESS-SIGNATURE").empty());
    EXPECT_FALSE(connect_request.headers.at("KALSHI-ACCESS-TIMESTAMP").empty());
    EXPECT_TRUE(transport.connected());
    EXPECT_EQ(transport.config().endpoint, connect_request.endpoint);
}

TEST(WsSessionTest, SubscribeSendsExpectedPayload) {
    const auto kalshi_adapter = make_kalshi_adapter();
    trading::tests::FakeWsTransport transport;
    trading::adapters::ws::WsSession session{transport, kalshi_adapter};

    ASSERT_TRUE(session.connect());
    ASSERT_TRUE(session.subscribe("trades"));
    ASSERT_EQ(transport.sent_messages().size(), 1U);

    const auto subscribe_json = nlohmann::json::parse(session.last_subscribe_payload());
    EXPECT_EQ(subscribe_json.at("cmd"), "subscribe");
    EXPECT_EQ(subscribe_json.at("params").at("channels").at(0), "trades");
}

} // namespace
