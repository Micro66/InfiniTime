#include "CaptiveDns.h"
#include <array>
#include <cassert>
#include <iostream>
#include <vector>

int main() {
  using Esp32::PhotoNetwork::DnsReply;
  std::vector<uint8_t> query {0x12, 0x34, 1, 0,   0,   1,   0,   0,   0, 0,   0,   0,   7, 'c', 'a', 'p', 't', 'i',
                              'v',  'e',  5, 'a', 'p', 'p', 'l', 'e', 3, 'c', 'o', 'm', 0, 0,   1,   0,   1};
  std::array<uint8_t, 528> reply {};
  const auto count = DnsReply(query.data(), query.size(), reply.data(), reply.size());
  assert(count == query.size() + 16 && reply[0] == 0x12 && reply[1] == 0x34);
  assert(reply[2] == 0x85 && reply[3] == 0 && reply[7] == 1);
  assert(reply[count - 4] == 192 && reply[count - 3] == 168 && reply[count - 2] == 4 && reply[count - 1] == 1);
  assert(DnsReply(query.data(), query.size(), reply.data(), count - 1) == 0);
  for (size_t n = 0; n < query.size(); ++n)
    assert(DnsReply(query.data(), n, reply.data(), reply.size()) == 0);
  query[query.size() - 3] = 28; // IPv6 query must not receive an IPv4 RDATA answer.
  assert(DnsReply(query.data(), query.size(), reply.data(), reply.size()) == query.size() && reply[7] == 0);
  query[12] = 0xc0; // Compression/pointer loop is not accepted in the first question.
  assert(DnsReply(query.data(), query.size(), reply.data(), reply.size()) == 0);
  query[12] = 7;
  query[2] = 0x81; // Do not answer another server's reply.
  assert(DnsReply(query.data(), query.size(), reply.data(), reply.size()) == 0);
  query[2] = 1;
  query[5] = 2;
  assert(DnsReply(query.data(), query.size(), reply.data(), reply.size()) == 0);
  std::cout << "PASS: captive DNS A/AAAA, transaction ID, truncated/oversized labels, compression, response and question-count rejection\n";
}
