#include "test/integration/integration_admin_test.h"

INSTANTIATE_TEST_SUITE_P(Protocols, HystrixIntegrationTest,
                         testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParams(
                             {Http::CodecClient::Type::HTTP1, Http::CodecClient::Type::HTTP2},
                             {FakeHttpConnection::Type::HTTP1})),
                         HttpProtocolIntegrationTest::protocolTestParamsToString);

TEST_P(HystrixIntegrationTest, NoChunkEncoding) {
  initialize();
}
