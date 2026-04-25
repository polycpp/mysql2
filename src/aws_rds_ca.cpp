#include "aws_rds_ca.hpp"

#include <iterator>

namespace polycpp::mysql2::detail {
namespace {

#include "aws_rds_ca.inc"

}  // namespace

const std::vector<std::string>& aws_rds_ca_certificates() {
    static const std::vector<std::string> certs(
        std::begin(kAwsRdsCaCertificates),
        std::end(kAwsRdsCaCertificates));
    return certs;
}

}  // namespace polycpp::mysql2::detail
