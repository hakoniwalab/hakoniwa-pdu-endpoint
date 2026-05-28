#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "nlohmann/json.hpp"
#include "hakoniwa/pdu/endpoint.hpp"
#include "geometry_msgs/pdu_cpptype_conv_Twist.hpp"

namespace benchmarks::runner {

enum class CommType {
    SHM,
    TCP,
    UDP
};

struct BenchmarkConfig {
    std::string benchmark_config_path;
    std::string protocol;
    std::string log_path;
    CommType comm_type;
    int try_num;
    int timeout_sec;
};

class Runner {
public:
    virtual ~Runner() = default;

    void load_benchmark_config(const std::string& config_path)
    {
        std::ifstream ifs(config_path);
        if (!ifs.is_open()) {
            throw std::runtime_error("Failed to open benchmark config file: " + config_path);
        }

        nlohmann::json config;
        try {
            ifs >> config;
        } catch (const nlohmann::json::exception& e) {
            throw std::runtime_error("JSON parsing failed for benchmark config: " + config_path + ". Details: " + e.what());
        }

        try {
            if (config.contains("protocol")) {
                std::string protocol = config["protocol"].get<std::string>();
                benchmark_config_.protocol = protocol;
                if (protocol == "shm") {
                    benchmark_config_.comm_type = CommType::SHM;
                } else if (protocol == "tcp") {
                    benchmark_config_.comm_type = CommType::TCP;
                } else if (protocol == "udp") {
                    benchmark_config_.comm_type = CommType::UDP;
                } else {
                    throw std::runtime_error("Unknown protocol in benchmark config: " + protocol);
                }
            } else {
                throw std::runtime_error("Benchmark config missing 'protocol': " + config_path);
            }

            if (config.contains("try_num")) {
                benchmark_config_.try_num = config["try_num"].get<int>();
            } else {
                throw std::runtime_error("Benchmark config missing 'try_num': " + config_path);
            }
            if (config.contains("config_path")) {
                benchmark_config_.benchmark_config_path = config["config_path"].get<std::string>();
            } else {
                throw std::runtime_error("Benchmark config missing 'config_path': " + config_path);
            }
            benchmark_config_.timeout_sec = config.value("timeout_sec", 30);
            benchmark_config_.log_path = config.value(
                "log_path",
                benchmark_config_.benchmark_config_path + "/benchmark-" + benchmark_config_.protocol + ".log");
        } catch (const nlohmann::json::exception& e) {
            throw std::runtime_error("JSON access failed for benchmark config: " + config_path + ". Details: " + e.what());
        }
    }

    virtual void prepare() = 0;
    virtual void run() = 0;
    virtual void cleanup() = 0;

protected:
    static std::uint64_t now_ns()
    {
        return static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count());
    }

    std::filesystem::path log_path_for_role(const char* role) const
    {
        std::filesystem::path base(benchmark_config_.log_path);
        const auto parent = base.parent_path();
        const auto stem = base.stem().string();
        const auto ext = base.extension().string();

        std::string filename;
        if (!stem.empty() && ext == ".log") {
            filename = stem + "_" + role + ext;
        } else if (!base.filename().empty()) {
            filename = base.filename().string() + "_" + role + ".log";
        } else {
            filename = std::string("benchmark-") + benchmark_config_.protocol + "_" + role + ".log";
        }
        return parent.empty() ? std::filesystem::path(filename) : parent / filename;
    }

    void open_benchmark_log(const char* role)
    {
        const auto path = log_path_for_role(role);
        const auto parent = path.parent_path();
        if (!parent.empty()) {
            std::filesystem::create_directories(parent);
        }

        benchmark_log_.open(path, std::ios::out | std::ios::trunc);
        if (!benchmark_log_.is_open()) {
            throw std::runtime_error("Failed to open benchmark log file: " + path.string());
        }
        std::cout << "Benchmark log: " << path.string() << std::endl;
    }

    void close_benchmark_log()
    {
        std::lock_guard<std::mutex> lock(log_mutex_);
        if (benchmark_log_.is_open()) {
            benchmark_log_.flush();
            benchmark_log_.close();
        }
    }

    void write_benchmark_log(const std::string& line)
    {
        std::lock_guard<std::mutex> lock(log_mutex_);
        if (benchmark_log_.is_open()) {
            benchmark_log_ << line << std::endl;
        } else {
            std::cout << line << std::endl;
        }
    }

    void prepare_pdudefs(int num)
    {
        auto pdu_def = endpoint_->get_pdu_definition();
        if (!pdu_def) {
            throw std::runtime_error("PDU definition is not available in endpoint");
        }
        hakoniwa::pdu::PduKey key_org = {"Drone-1", "pos"};
        size_t pdu_size = endpoint_->get_pdu_size(key_org);
        int channel_id = endpoint_->get_pdu_channel_id(key_org);
        if (pdu_size == 0) {
            throw std::runtime_error(
                "Failed to get PDU size for key: " + key_org.robot + "/" + key_org.pdu);
        }
        for (int i = 1; i < num; ++i) {
            pdu_def->add_definition("Drone-" + std::to_string(i + 1), {
                .type = "geometry_msgs/Twist",
                .org_name = key_org.pdu,
                .channel_id = channel_id,
                .pdu_size = pdu_size
            });
        }
    }

    void create_send_buffer_for_key(int num)
    {
        pdu_keys_.clear();
        pdu_sizes_.clear();
        buf_.clear();
        send_size_ = 0;
        for (int i = 0; i < num; ++i) {
            hakoniwa::pdu::PduKey key = {"Drone-" + std::to_string(i + 1), "pos"};

            size_t pdu_size = endpoint_->get_pdu_size(key);
            if (pdu_size == 0) {
                throw std::runtime_error(
                    "Failed to get PDU size for key: " + key.robot + "/" + key.pdu);
            }

            pdu_keys_.push_back(key);
            pdu_sizes_.push_back(pdu_size);
        }

        if (pdu_sizes_.empty()) {
            throw std::runtime_error("No PDU keys were created");
        }

        hakoniwa::pdu::PduKey key = pdu_keys_[0];
        buf_.resize(pdu_sizes_[0]);

        HakoCpp_Twist twist{};
        twist.linear.x = 0.0;
        twist.linear.y = 0.1;
        twist.linear.z = 0.2;
        twist.angular.x = 0.3;
        twist.angular.y = 0.4;
        twist.angular.z = 0.5;

        hako::pdu::msgs::geometry_msgs::Twist twist_convertor;
        send_size_ = twist_convertor.cpp2pdu(
            twist,
            reinterpret_cast<char*>(buf_.data()),
            static_cast<int>(buf_.size()));

        if (send_size_ <= 0) {
            std::cerr
                << "Failed to convert Twist to PDU for key: "
                << key.robot << "/" << key.pdu
                << std::endl;
            throw std::runtime_error("PDU conversion failed");
        }
    }

    void reset_receive_benchmark(int expected_count)
    {
        std::lock_guard<std::mutex> lock(recv_mutex_);
        expected_count_.store(expected_count);
        received_count_.store(0);
        first_recv_ns_ = 0;
        last_recv_ns_ = 0;
    }

    void record_receive_event(const char* protocol,
                              const hakoniwa::pdu::PduResolvedKey& received_key,
                              std::span<const std::byte> data)
    {
        const auto ts = now_ns();
        const int count = received_count_.fetch_add(1) + 1;
        {
            std::lock_guard<std::mutex> lock(recv_mutex_);
            if (first_recv_ns_ == 0) {
                first_recv_ns_ = ts;
            }
            last_recv_ns_ = ts;
        }

        std::ostringstream oss;
        oss << "BENCH_SUB_EVENT protocol=" << protocol
            << " robot=" << received_key.robot
            << " channel=" << received_key.channel_id
            << " size=" << data.size()
            << " count=" << count
            << " recv_ns=" << ts;
        write_benchmark_log(oss.str());

        if (count >= expected_count_.load()) {
            recv_cv_.notify_all();
        }
    }

    void wait_receive_benchmark(const char* protocol)
    {
        std::unique_lock<std::mutex> lock(recv_mutex_);
        const auto timeout = std::chrono::seconds(benchmark_config_.timeout_sec);
        const bool completed = recv_cv_.wait_for(lock, timeout, [this]() {
            return received_count_.load() >= expected_count_.load();
        });

        const int received = received_count_.load();
        const int expected = expected_count_.load();
        const auto first = first_recv_ns_;
        const auto last = last_recv_ns_;
        const double recv_span_ms = (first != 0 && last >= first)
            ? static_cast<double>(last - first) / 1000000.0
            : 0.0;

        std::ostringstream oss;
        oss << "BENCH_SUB_SUMMARY protocol=" << protocol
            << " expected=" << expected
            << " received=" << received
            << " first_recv_ns=" << first
            << " last_recv_ns=" << last
            << " recv_span_ms=" << recv_span_ms
            << " completed=" << (completed ? 1 : 0);
        write_benchmark_log(oss.str());

        if (!completed) {
            throw std::runtime_error(
                std::string("Timed out waiting for ") + protocol +
                " PDUs: received=" + std::to_string(received) +
                " expected=" + std::to_string(expected));
        }
    }

    std::unique_ptr<hakoniwa::pdu::Endpoint> endpoint_;
    std::vector<hakoniwa::pdu::PduKey> pdu_keys_;
    std::vector<size_t> pdu_sizes_;
    std::vector<std::byte> buf_;
    int send_size_ = 0;
    BenchmarkConfig benchmark_config_;

    std::atomic<int> expected_count_{0};
    std::atomic<int> received_count_{0};
    std::uint64_t first_recv_ns_ = 0;
    std::uint64_t last_recv_ns_ = 0;
    std::mutex recv_mutex_;
    std::condition_variable recv_cv_;
    std::ofstream benchmark_log_;
    std::mutex log_mutex_;
};

}
